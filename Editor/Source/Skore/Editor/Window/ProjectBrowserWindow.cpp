#include "ProjectBrowserWindow.hpp"

#include <regex>

#include "imgui_internal.h"
#include "Skore/Engine.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Editor/Editor.hpp"
#include "Skore/Editor/ImGui/ImGuiEditor.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/Platform/Platform.hpp"
#include "Skore/Scene/Scene.hpp"

#define CONTENT_TABLE_ID 500

namespace Skore
{
    MenuItemContext ProjectBrowserWindow::menuItemContext = {};

    void ProjectBrowserWindow::Init(u32 id, VoidPtr userData)
    {
        openDirectory = AssetEditor::GetAssetFolder();
    }

    void ProjectBrowserWindow::DrawPathItems() {}

    void ProjectBrowserWindow::DrawTreeNode(AssetFile* file)
    {
        if (!file || !file->isDirectory) return;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;

        bool openDir = openTreeFolders[file->absolutePath];


        if (!openDir && openDirectory != nullptr && openDirectory->IsChildOf(file))
        {
            openTreeFolders[file->absolutePath] = true;
            openDir = true;
        }

        if (openDir)
        {
            ImGui::SetNextItemOpen(true);
        }

        if (openDirectory && openDirectory == file)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        stringCache.Clear();
        if (openDir)
        {
            stringCache = ICON_FA_FOLDER_OPEN;
        }
        else
        {
            stringCache = ICON_FA_FOLDER;
        }

        stringCache.Append(" ").Append(file->fileName);
        bool isNodeOpen = ImGui::TreeNode(file->hash, stringCache.CStr(), flags);

        if (ImGui::BeginDragDropTarget())
        {
            if (file->canAcceptNewAssets)
            {
                bool canAcceptDragDrop = false;

                for(auto& it: selectedItems)
                {
                    if (!file->IsChildOf(it.first))
                    {
                        canAcceptDragDrop = true;
                        break;
                    }
                }

                if (canAcceptDragDrop && ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD))
                {
                    for (auto& it : selectedItems)
                    {
                        if (!file->IsChildOf(it.first))
                        {
                            it.first->MoveTo(file);
                        }
                    }
                }
            }

            ImGui::EndDragDropTarget();
        }

        if (file->parent != nullptr && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover | ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
        {
            AssetPayload payload ={
                .assetFile = file,
                .assetType = file->handler != nullptr ? file->handler->GetAssetTypeID() : 0
            };
            ImGui::SetDragDropPayload(SK_ASSET_PAYLOAD, &payload, sizeof(AssetPayload));

            ImGui::Text("%s", file->fileName.CStr());
            ImGui::EndDragDropSource();
        }

        if (openDir == isNodeOpen && ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            SetOpenDirectory(file);
        }

        openTreeFolders[file->absolutePath] = isNodeOpen;

        if (isNodeOpen)
        {
            for (AssetFile* childNode : file->children)
            {
                DrawTreeNode(childNode);
            }
            ImGui::TreePop();
        }
    }

    void ProjectBrowserWindow::Draw(u32 id, bool& open)
    {
        String labelCache = "";

        ImGuiStyle& style = ImGui::GetStyle();
        ImVec2      pad = style.WindowPadding;

        bool readOnly = openDirectory == nullptr;

        ImGui::StyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::StyleVar cellPadding(ImGuiStyleVar_CellPadding, ImVec2(0, 0));

        ImGui::StyleColor tableBorderStyleColor(ImGuiCol_TableBorderLight, IM_COL32(0, 0, 0, 0));

        ImGui::Begin(id, ICON_FA_FOLDER " Project Browser", &open, ImGuiWindowFlags_NoScrollbar);


        //top child
        {
            ImVec2          a = ImVec2(pad.x / 1.5f, pad.y / 1.5f);
            ImGui::StyleVar childPadding(ImGuiStyleVar_WindowPadding, a);
            f32             width = ImGui::GetContentRegionAvail().x - a.x;
            ImGui::BeginChild(id + 5, ImVec2(width, 30 * style.ScaleFactor), false, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar);

            ImGui::BeginHorizontal((i32)id + 10, ImVec2(width - a.x - pad.x, 0));

            ImGui::BeginDisabled(readOnly);
            if (ImGui::Button(ICON_FA_PLUS " Import"))
            {
                Array<String> paths{};
                Array<FileFilter> filters;
                //assetEditor.FilterExtensions(filters);

                if (Platform::OpenDialogMultiple(paths, filters, {}) == DialogResult::OK)
                {
                    if (!paths.Empty())
                    {
                        AssetEditor::ImportAssets(openDirectory, paths);
                    }
                }
            }
            ImGui::EndDisabled();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 0.f, 0.f, 0.f));

            DrawPathItems();

            ImGui::Spring(1.0f);

            ImGui::PopStyleColor(2);

            ImGui::SetNextItemWidth(250 * style.ScaleFactor);
            ImGui::SliderFloat("###zoom", &contentBrowserZoom, 0.4f, 5.0f, "");

            ImGui::SetNextItemWidth(400 * style.ScaleFactor);
            ImGui::SearchInputText(id + 20, searchString);


            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 0.f, 0.f, 0.f));

            if (ImGui::Button(ICON_FA_GEAR " Settings")) {}

            ImGui::PopStyleColor(2);
            ImGui::EndHorizontal();

            ImGui::EndChild();
        }

        auto* drawList = ImGui::GetWindowDrawList();

        auto p1 = ImGui::GetCursorScreenPos();
        auto p2 = ImVec2(ImGui::GetContentRegionAvail().x + p1.x, p1.y);
        drawList->AddLine(p1, p2, IM_COL32(0, 0, 0, 255), 1.f * style.ScaleFactor);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.f * style.ScaleFactor);

        bool browseFolder = true;

        static ImGuiTableFlags flags = ImGuiTableFlags_Resizable;

        if (ImGui::BeginTable("table-project-browser", browseFolder ? 2 : 1, flags))
        {
            ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, 300 * style.ScaleFactor);
            ImGui::TableNextColumn();
            {
                ImGui::StyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
                ImGui::StyleVar   rounding(ImGuiStyleVar_FrameRounding, 0);
                ImGui::BeginChild(52110);

                ImGui::BeginTreeNode();

                for (const auto& package : AssetEditor::GetPackages())
                {
                    DrawTreeNode(package);
                }

                DrawTreeNode(AssetEditor::GetProject());

                ImGui::EndTreeNode();
                ImGui::EndChild();
            }

             auto rx = std::regex{searchString.CStr(), std::regex_constants::icase};

            ImGui::TableNextColumn();
            {
                ImGui::StyleColor childBg(ImGuiCol_ChildBg, IM_COL32(27, 28, 30, 255));
                auto              padding = 0;
                ImGui::StyleVar   cellPadding(ImGuiStyleVar_CellPadding, ImVec2(padding, padding));
                ImGui::StyleVar   itemSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(contentBrowserZoom, contentBrowserZoom));
                ImGui::StyleVar   framePadding(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                ImGui::StyleVar   browserWinPadding(ImGuiStyleVar_WindowPadding, ImVec2(5.f * style.ScaleFactor, 5.f * style.ScaleFactor));

                ImGui::BeginChild(52211, ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding);

                ImGui::SetWindowFontScale(contentBrowserZoom);

                if (ImGui::BeginContentTable("ProjectBrowser", contentBrowserZoom))
                {
                    AssetFile* newOpenDirectory = nullptr;

                    AssetFile* moveAssetsTo = nullptr;

                    if (openDirectory != nullptr)
                    {
                        for (int i = 0; i < 2; ++i)
                        {
                            for (AssetFile* assetFile : openDirectory->children)
                            {
                                if (!assetFile->active) continue;

                                //workaround to show directories first.
                                if (i == 0 && !assetFile->isDirectory) continue;
                                if (i == 1 && assetFile->isDirectory) continue;


                                //TODO slow!!!! cache it on AssetEditor.
                                if (!searchString.Empty())
                                {
                                    if (!std::regex_search(assetFile->fileName.CStr(), rx))
                                    {
                                        continue;
                                    }
                                }

                                labelCache.Clear();

                                bool renaming = renamingItem == assetFile;

                                if (!renaming && assetFile->IsDirty())
                                {
                                    labelCache = "*";
                                }

                                labelCache += assetFile->fileName;

                                if (!renaming)
                                {
                                    labelCache += assetFile->extension;
                                }

                                ImGui::ContentItemDesc desc;
                                desc.id = reinterpret_cast<usize>(assetFile);
                                desc.label = labelCache.CStr();
                                desc.texture = assetFile->GetThumbnail();
                                desc.renameItem = renaming;
                                desc.thumbnailScale = contentBrowserZoom;
                                desc.selected = selectedItems.Has(assetFile);

                                ImGui::ContentItemState state = ImGui::ContentItem(desc);

                                if (state.clicked)
                                {
                                    if (!(ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_RightCtrl))))
                                    {
                                        selectedItems.Clear();
                                        lastSelectedItem = nullptr;
                                    }
                                    selectedItems.Emplace(assetFile);
                                    lastSelectedItem = assetFile;
                                    newSelection = true;
                                }

                                if (state.doubleClicked)
                                {
                                    if (assetFile->isDirectory)
                                    {
                                        newOpenDirectory = assetFile;
                                        selectedItems.Clear();
                                        lastSelectedItem = nullptr;
                                    }
                                    else if (assetFile->handler)
                                    {
                                        assetFile->handler->OpenAsset(assetFile);
                                    }
                                }

                                if (state.renameFinish)
                                {
                                    if (!state.newName.Empty())
                                    {
                                        AssetEditor::Rename(assetFile, state.newName);
                                    }
                                    renamingItem = nullptr;
                                }

                                ImGui::SetCursorScreenPos(ImVec2(state.screenStartPos.x + 3 * style.ScaleFactor, state.screenStartPos.y + 3 * style.ScaleFactor));
                                ImGui::PushID(desc.id + 678);
                                ImGui::InvisibleButton("", ImVec2(state.size.x - 7 * style.ScaleFactor, state.size.y - 6 * style.ScaleFactor));

                                if (assetFile->isDirectory && ImGui::BeginDragDropTarget())
                                {
                                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD))
                                    {
                                        moveAssetsTo = assetFile;
                                    }
                                    ImGui::EndDragDropTarget();
                                }

                                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
                                {
                                    AssetPayload payload ={
                                        .assetFile = assetFile,
                                        .assetType = assetFile->handler != nullptr ? assetFile->handler->GetAssetTypeID() : 0
                                    };

                                    ImGui::SetDragDropPayload(SK_ASSET_PAYLOAD, &payload, sizeof(AssetPayload));
                                    ImGui::Text("%s", desc.label.CStr());
                                    ImGui::EndDragDropSource();
                                }

                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip())
                                {
                                    ImGui::TextUnformatted(labelCache.CStr());
                                    ImGui::Separator();


                                    ImGui::TextWithLabel("Relative Path: ", assetFile->path);
                                    ImGui::TextWithLabel("UUID: ", assetFile->uuid.ToString());


                                    ImGui::EndTooltip();
                                }

                                ImGui::SetCursorScreenPos(state.screenStartPos);

                                ImGui::PopID();
                            }
                        }
                    }

                    ImGui::EndContentTable();

                    if (newOpenDirectory)
                    {
                        SetOpenDirectory(newOpenDirectory);
                    }

                    if (moveAssetsTo)
                    {
                        for(auto& it: selectedItems)
                        {
                            it.first->MoveTo(moveAssetsTo);
                        }
                        selectedItems.Clear();
                        lastSelectedItem = nullptr;
                    }
                }

                ImGui::SetWindowFontScale(1.0);
                ImGui::EndChild();
            }
            ImGui::EndTable();
        }


        bool closePopup = false;
        if (!renamingItem && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        {
            if (menuItemContext.ExecuteHotKeys(this))
            {
                closePopup = true;
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
            {
                ImGui::OpenPopup("project-browser-popup");
            }
        }

        auto popupRes = ImGui::BeginPopupMenu("project-browser-popup");
        if (popupRes)
        {
            menuItemContext.Draw(this);
            if (closePopup)
            {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopupMenu(popupRes);

        if (!popupRes && !newSelection && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        {
            selectedItems.Clear();
            lastSelectedItem = nullptr;
        }

        newSelection = false;

        ImGui::End();
    }

    void ProjectBrowserWindow::SetOpenDirectory(AssetFile* directory)
    {
        selectedItems.Clear();
        selectedItems.Emplace(directory);
        lastSelectedItem = directory;
        newSelection = true;

        openDirectory = directory;
        if (directory && directory->parent)
        {
            openTreeFolders[directory->parent->absolutePath] = true;
        }
    }

    bool ProjectBrowserWindow::CheckSelectedAsset(const MenuItemEventData& eventData)
    {
        ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
        return projectBrowserWindow->lastSelectedItem != nullptr;
    }

    void ProjectBrowserWindow::AssetRename(const MenuItemEventData& eventData)
    {
        ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
        projectBrowserWindow->renamingItem = projectBrowserWindow->lastSelectedItem;
    }

    void ProjectBrowserWindow::Shutdown()
    {
        menuItemContext = MenuItemContext{};
    }

    void ProjectBrowserWindow::AssetNewFolder(const MenuItemEventData& eventData)
    {
        ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
        AssetFile*            newDirectory = AssetEditor::CreateDirectory(projectBrowserWindow->openDirectory);

        projectBrowserWindow->renamingItem = newDirectory;
        projectBrowserWindow->selectedItems.Clear();
        projectBrowserWindow->selectedItems.Insert(newDirectory);
        projectBrowserWindow->lastSelectedItem = newDirectory;
    }

    void ProjectBrowserWindow::AssetNew(const MenuItemEventData& eventData)
    {
        ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
        if (AssetFile* newAsset = AssetEditor::CreateAsset(projectBrowserWindow->openDirectory, eventData.userData))
        {
            projectBrowserWindow->renamingItem = newAsset;
            projectBrowserWindow->selectedItems.Clear();
            projectBrowserWindow->selectedItems.Insert(newAsset);
            projectBrowserWindow->lastSelectedItem = newAsset;
        }
    }

    void ProjectBrowserWindow::AssetDelete(const MenuItemEventData& eventData)
    {
        ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);

        Array<AssetFile*> assets;
        assets.Reserve(projectBrowserWindow->selectedItems.Size());

        for (auto it: projectBrowserWindow->selectedItems)
        {
            assets.EmplaceBack(it.first);
        }

        AssetEditor::DeleteAssets(assets);
    }

    void ProjectBrowserWindow::AssetShowInExplorer(const MenuItemEventData& eventData)
    {
        ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
        if (projectBrowserWindow->lastSelectedItem)
        {
            Platform::ShowInExplorer(projectBrowserWindow->lastSelectedItem->absolutePath);
        }
        else if (projectBrowserWindow->openDirectory)
        {
            Platform::ShowInExplorer(projectBrowserWindow->openDirectory->absolutePath);
        }
    }

    void ProjectBrowserWindow::AssetCopyPathToClipboard(const MenuItemEventData& eventData)
    {
        ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
        if (projectBrowserWindow->lastSelectedItem)
        {
            Platform::SetClipboardString(Engine::GetActiveWindow(), projectBrowserWindow->lastSelectedItem->path);
        }
    }

    bool ProjectBrowserWindow::CanCreateAsset(const MenuItemEventData& eventData)
    {
        ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
        return projectBrowserWindow->openDirectory != nullptr && projectBrowserWindow->openDirectory->canAcceptNewAssets;
    }

    void ProjectBrowserWindow::OpenProjectBrowser(const MenuItemEventData& eventData)
    {
        Editor::OpenWindow(GetTypeID<ProjectBrowserWindow>());
    }

    void ProjectBrowserWindow::AddMenuItem(const MenuItemCreation& menuItem)
    {
        menuItemContext.AddMenuItem(menuItem);
    }

    void ProjectBrowserWindow::RegisterType(NativeTypeHandler<ProjectBrowserWindow>& type)
    {
        Event::Bind<OnShutdown, Shutdown>();

        Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Project Browser", .action = OpenProjectBrowser});

        AddMenuItem(MenuItemCreation{.itemName = "New Folder", .icon = ICON_FA_FOLDER, .priority = 0, .action = AssetNewFolder, .enable = CanCreateAsset});
        AddMenuItem(MenuItemCreation{.itemName = "New Scene", .icon = ICON_FA_CLAPPERBOARD, .priority = 10, .action = AssetNew, .enable = CanCreateAsset, .userData = GetTypeID<Scene>()});
        AddMenuItem(MenuItemCreation{.itemName = "New Material", .icon = ICON_FA_PAINTBRUSH, .priority = 15, .action = AssetNew, .enable = CanCreateAsset, .userData = GetTypeID<MaterialAsset>()});
        AddMenuItem(MenuItemCreation{.itemName = "Delete", .icon = ICON_FA_TRASH, .priority = 20, .itemShortcut{.presKey = Key::Delete}, .action = AssetDelete, .enable = CheckSelectedAsset});
        AddMenuItem(MenuItemCreation{.itemName = "Rename", .icon = ICON_FA_PEN_TO_SQUARE, .priority = 30, .itemShortcut{.presKey = Key::F2}, .action = AssetRename, .enable = CheckSelectedAsset});
        AddMenuItem(MenuItemCreation{.itemName = "Show in Explorer", .icon = ICON_FA_FOLDER, .priority = 40, .action = AssetShowInExplorer});
        //AddMenuItem(MenuItemCreation{.itemName = "Reimport", .icon = ICON_FA_REPEAT, .priority = 50, .action = AssetReimport, .enable = CheckCanReimport});
        AddMenuItem(MenuItemCreation{.itemName = "Copy Path", .priority = 1000, .action = AssetCopyPathToClipboard});
        //AddMenuItem(MenuItemCreation{.itemName = "Create New Asset", .icon = ICON_FA_PLUS, .priority = 150});
        // AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Shader", .icon = ICON_FA_BRUSH, .priority = 10, .action = AssetNew, .menuData = (VoidPtr)GetTypeID<ShaderAsset>()});
        // AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Resource Graph", .icon = ICON_FA_DIAGRAM_PROJECT, .priority = 10, .action = AssetNewResourceGraph});
        // AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Behavior Graph", .icon = ICON_FA_DIAGRAM_PROJECT, .priority = 20});
        // AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Environment", .icon = ICON_FA_GLOBE, .priority = 10, .action = AssetNew, .userData = 0});


        type.Attribute<EditorWindowProperties>(EditorWindowProperties{
            .dockPosition = DockPosition::Bottom,
            .createOnInit = true
        });
    }
}
