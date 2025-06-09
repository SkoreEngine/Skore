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

#include "ProjectBrowserWindow.hpp"

#include "SDL3/SDL_dialog.h"
#include "SDL3/SDL_misc.h"
#include "Skore/Editor.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Scene/SceneTypes.hpp"
#include "Skore/Core/StaticContent.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/ResourceType.hpp"


namespace Skore
{

	struct ProjectBrowserWindowData
	{
		enum
		{
			OpenDirectory,
			RenamingItem,
			SelectedItems,
			LastSelectedItem
		};
	};

	namespace
	{
		ProjectBrowserWindow* lastOpenedWindow = nullptr;
	}

	SDL_Window* GraphicsGetWindow();


	MenuItemContext ProjectBrowserWindow::menuItemContext = {};

	GPUTexture* directoryTexture;
	GPUTexture* assertTexture;

	void ProjectBrowserWindow::OnDropFile(StringView filePath)
	{
		if (lastOpenedWindow)
		{
			ResourceAssets::ImportAssets(lastOpenedWindow->GetOpenDirectory(), {filePath});
		}
	}

	void ProjectBrowserWindow::DrawPathItems()
	{

	}

	void ProjectBrowserWindow::DrawDirectoryTreeNode(RID rid)
	{
		if (!rid) return;
		if (Resources::GetStorage(rid)->resourceType->GetID() != TypeInfo<ResourceAssetDirectory>::ID())
		{
			return;
		}

		ResourceObject windowObject = Resources::Read(windowObjectRID);
		RID openDirectory = windowObject.GetReference(ProjectBrowserWindowData::OpenDirectory);

		ResourceObject directoryObject = Resources::Read(rid);
		RID asset = directoryObject.GetSubObject(ResourceAssetDirectory::DirectoryAsset);

		ResourceObject assetObject = Resources::Read(asset);

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
		bool openDir = openTreeFolders[asset];

		if (!openDir && openDirectory && ResourceAssets::IsChildOf(rid, GetOpenDirectory()))
		{
			openTreeFolders[rid] = true;
			openDir = true;
		}

		if (openDir)
		{
			ImGui::SetNextItemOpen(true);
		}

		if (openDirectory && openDirectory == rid)
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

		stringCache.Append(" ");
		stringCache.Append(assetObject.GetString(ResourceAsset::Name));
		stringCache.Append(assetObject.GetString(ResourceAsset::Extension));

		bool isNodeOpen = ImGuiTreeNode(reinterpret_cast<void*>((usize)asset.id), stringCache.CStr(), flags);

		if (ImGui::BeginDragDropTarget())
		{
			// if (file->canAcceptNewAssets)
			// {
			//     bool canAcceptDragDrop = false;
			//
			//     for(auto& it: selectedItems)
			//     {
			//         if (!file->IsChildOf(it.first))
			//         {
			//             canAcceptDragDrop = true;
			//             break;
			//         }
			//     }
			//
			//     if (canAcceptDragDrop && ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD))
			//     {
			//         for (auto& it : selectedItems)
			//         {
			//             if (!file->IsChildOf(it.first))
			//             {
			//                 it.first->MoveTo(file);
			//             }
			//         }
			//     }
			// }

			if (const ImGuiPayload* externalPayload = ImGui::AcceptDragDropPayload("EXTERNAL_ASSET", ImGuiDragDropFlags_SourceExtern))
			{
				int a = 0;
			}

			ImGui::EndDragDropTarget();
		}


		// if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern))
		// {
		//

		//
		//     ImGui::EndDragDropSource();
		// }


		// if (file->parent != nullptr && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover | ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
		// {
		//     AssetPayload payload ={
		//         .assetFile = file,
		//         .assetType = file->handler != nullptr ? file->handler->GetAssetTypeID() : 0
		//     };
		//     ImGui::SetDragDropPayload(SK_ASSET_PAYLOAD, &payload, sizeof(AssetPayload));
		//
		//     ImGui::Text("%s", file->fileName.CStr());
		//     ImGui::EndDragDropSource();
		// }

		if (openDir == isNodeOpen && ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			SetOpenDirectory(rid);
		}

		openTreeFolders[asset] = isNodeOpen;

		if (isNodeOpen)
		{
			Array<RID> children = directoryObject.GetSubObjectSetAsArray(ResourceAssetDirectory::Directories);
			for (RID child : children)
			{
				DrawDirectoryTreeNode(child);
			}
			ImGui::TreePop();
		}
	}

	void ProjectBrowserWindow::SetOpenDirectory(RID rid)
	{
		SK_ASSERT(Resources::GetStorage(rid)->resourceType->GetID() == TypeInfo<ResourceAssetDirectory>::ID(), "rid is not a AssetDirectory");

		ResourceObject windowObject = Resources::Write(windowObjectRID);
		windowObject.SetReference(ProjectBrowserWindowData::OpenDirectory, rid);
		windowObject.Commit();
	}

	ProjectBrowserWindow::~ProjectBrowserWindow()
	{
		if (lastOpenedWindow == this)
		{
			lastOpenedWindow = nullptr;
		}
	}

	void ProjectBrowserWindow::Init(u32 id, VoidPtr userData)
	{
		ResourceObject packageProject = Resources::Read(Editor::GetProject());
		windowObjectRID = Resources::Create<ProjectBrowserWindowData>();
		ResourceObject obj = Resources::Write(windowObjectRID);
		obj.SetReference(ProjectBrowserWindowData::OpenDirectory, packageProject.GetSubObject(ResourceAssetPackage::Root));
		obj.Commit();
	}

	void ProjectBrowserWindow::Draw(u32 id, bool& open)
	{
		lastOpenedWindow = this;

		ResourceObject windowObject = Resources::Read(windowObjectRID);
		RID openDirectory = windowObject.GetReference(ProjectBrowserWindowData::OpenDirectory);

		String labelCache = "";

		ImGuiStyle&     style = ImGui::GetStyle();
		ImVec2          pad = style.WindowPadding;
		bool            readOnly = false;
		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ScopedStyleVar cellPadding(ImGuiStyleVar_CellPadding, ImVec2(0, 0));

		ScopedStyleColor tableBorderStyleColor(ImGuiCol_TableBorderLight, IM_COL32(0, 0, 0, 0));
		ImGuiBegin(id, ICON_FA_FOLDER " Project Browser", &open, ImGuiWindowFlags_NoScrollbar);

		//top child
		{
			ImVec2          a = ImVec2(pad.x / 1.5f, pad.y / 1.5f);
			ScopedStyleVar childPadding(ImGuiStyleVar_WindowPadding, a);
			f32             width = ImGui::GetContentRegionAvail().x - a.x;
			ImGui::BeginChild(id + 5, ImVec2(width, 30 * style.ScaleFactor), false, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar);

			ImGui::BeginHorizontal((i32)id + 10, ImVec2(width - a.x - pad.x, 0));

			ImGui::BeginDisabled(readOnly);
			if (ImGui::Button(ICON_FA_PLUS " Import"))
			{
				SDL_ShowOpenFileDialog([](void* userdata, const char* const * filelist, int filter)
				{

				},
				nullptr,
				GraphicsGetWindow(), nullptr, 0, nullptr, true);

				// Array<String> paths{};
				// Array<FileFilter> filters;
				// //assetEditor.FilterExtensions(filters);
				//
				// if (Platform::OpenDialogMultiple(paths, filters, {}) == DialogResult::OK)
				// {
				//     if (!paths.Empty())
				//     {
				//         ResourceAssets::ImportAssets(openDirectory, paths);
				//     }
				// }
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
			ImGuiSearchInputText(id + 20, searchString);


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
				ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(22, 23, 25, 255));
				ScopedStyleVar   rounding(ImGuiStyleVar_FrameRounding, 0);
				ImGui::BeginChild(52110);

				ImGuiBeginTreeNodeStyle();

				auto drawPackage = [&](RID package)
				{
					ResourceObject packageProject = Resources::Read(package);
					DrawDirectoryTreeNode(packageProject.GetSubObject(ResourceAssetPackage::Root));
				};

				for (const auto& package : Editor::GetPackages())
				{
					drawPackage(package);
				}

				drawPackage(Editor::GetProject());

				ImGuiEndTreeNodeStyle();
				ImGui::EndChild();
			}

			ImGui::TableNextColumn();
			{
				ScopedStyleColor childBg(ImGuiCol_ChildBg, IM_COL32(27, 28, 30, 255));
				auto             padding = 0;
				ScopedStyleVar   cellPadding(ImGuiStyleVar_CellPadding, ImVec2(padding, padding));
				ScopedStyleVar   itemSpacing(ImGuiStyleVar_ItemSpacing, ImVec2(contentBrowserZoom, contentBrowserZoom));
				ScopedStyleVar   framePadding(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
				ScopedStyleVar   browserWinPadding(ImGuiStyleVar_WindowPadding, ImVec2(5.f * style.ScaleFactor, 5.f * style.ScaleFactor));

				ImGui::BeginChild(52211, ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding);

				ImGui::SetWindowFontScale(contentBrowserZoom);

				if (ImGuiBeginContentTable("ProjectBrowser", contentBrowserZoom))
				{
					RID renamingItem = windowObject.GetReference(ProjectBrowserWindowData::RenamingItem);

					RID newOpenDirectory = {};
					if (openDirectory)
					{
						auto drawContentItem = [&](RID asset, bool isDirectory)
						{
							ResourceObject assetObject = Resources::Read(asset);

							labelCache.Clear();

							bool renaming = renamingItem == asset;

							if (!renaming && ResourceAssets::IsUpdated(asset))
							{
								labelCache = "*";
							}

							labelCache += assetObject.GetString(ResourceAsset::Name);

							if (!renaming)
							{
								labelCache += assetObject.GetString(ResourceAsset::Extension);
							}

							ImGuiContentItemDesc desc;
							desc.id = asset.id;
							desc.label = labelCache.CStr();
							desc.texture = isDirectory ? directoryTexture : assertTexture;
							desc.thumbnailScale = contentBrowserZoom;
							desc.renameItem = renaming;
							desc.selected = windowObject.HasOnReferenceArray(ProjectBrowserWindowData::SelectedItems, asset);

							ImGuiContentItemState state = ImGuiContentItem(desc);

							if (state.clicked)
							{
								UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Selection");

								if (!(ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_RightCtrl))))
								{
									ClearSelection(scope);
								}
								SelectItem(asset, scope);

								newSelection = true;
							}

							if (state.renameFinish)
							{
								UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Rename Finished");
								if (!state.newName.Empty())
								{
									ResourceObject write = Resources::Write(asset);
									write.SetString(ResourceAsset::Name, state.newName);
									write.Commit(scope);
								}

								ResourceObject objWrite = Resources::Write(windowObjectRID);
								objWrite.SetReference(ProjectBrowserWindowData::RenamingItem, {});
								objWrite.Commit(scope);
							}

							return state;
						};
						ResourceObject openDirectoryObject = Resources::Read(openDirectory);

						for (RID directory: openDirectoryObject.GetSubObjectSetAsArray(ResourceAssetDirectory::Directories))
						{
							ResourceObject directoryObject = Resources::Read(directory);
							RID asset = directoryObject.GetSubObject(ResourceAssetDirectory::DirectoryAsset);
							ImGuiContentItemState state = drawContentItem(asset, true);
							if (state.enter)
							{
								newOpenDirectory = directory;
								ClearSelection(nullptr);
							}
						}

						for (RID asset : openDirectoryObject.GetSubObjectSetAsArray(ResourceAssetDirectory::Assets))
						{
							ImGuiContentItemState state = drawContentItem(asset, false);
							if (state.enter)
							{
								ResourceAssets::OpenAsset(asset);
							}
						}

						if (newOpenDirectory)
						{
							SetOpenDirectory(newOpenDirectory);
						}
					}
					ImGuiEndContentTable();
				}

				ImGui::SetWindowFontScale(1.0);
				ImGui::EndChild();
			}
			ImGui::EndTable();
		}

		bool closePopup = false;
		if (!windowObject.GetReference(ProjectBrowserWindowData::RenamingItem) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
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

		auto popupRes = ImGuiBeginPopupMenu("project-browser-popup");
		if (popupRes)
		{
			menuItemContext.Draw(this);
			if (closePopup)
			{
				ImGui::CloseCurrentPopup();
			}
		}
		ImGuiEndPopupMenu(popupRes);

		if (!popupRes && !newSelection && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		{
			ClearSelection(nullptr);
		}

		newSelection = false;

		ImGui::End();
	}

	void ProjectBrowserWindow::ClearSelection(UndoRedoScope* scope)
	{
		ResourceObject windowObject = Resources::Write(windowObjectRID);
		windowObject.ClearReferenceArray(ProjectBrowserWindowData::SelectedItems);
		windowObject.SetReference(ProjectBrowserWindowData::LastSelectedItem, {});
		windowObject.Commit(scope);
	}

	void ProjectBrowserWindow::SelectItem(RID rid, UndoRedoScope* scope)
	{
		ResourceObject windowObject = Resources::Write(windowObjectRID);
		windowObject.AddToReferenceArray(ProjectBrowserWindowData::SelectedItems, rid);
		windowObject.SetReference(ProjectBrowserWindowData::LastSelectedItem, rid);
		windowObject.Commit(scope);
	}

	void ProjectBrowserWindow::SetRenameItem(RID rid, UndoRedoScope* scope)
	{
		ResourceObject windowObject = Resources::Write(windowObjectRID);
		windowObject.SetReference(ProjectBrowserWindowData::RenamingItem, rid);
		windowObject.Commit(scope);
	}

	RID ProjectBrowserWindow::GetLastSelectedItem() const
	{
		ResourceObject windowObject = Resources::Read(windowObjectRID);
		return windowObject.GetReference(ProjectBrowserWindowData::LastSelectedItem);
	}

	RID ProjectBrowserWindow::GetOpenDirectory() const
	{
		ResourceObject windowObject = Resources::Read(windowObjectRID);
		return windowObject.GetReference(ProjectBrowserWindowData::OpenDirectory);
	}


	void ProjectBrowserWindow::OpenProjectBrowser(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow(TypeInfo<ProjectBrowserWindow>::ID());
	}

	void ProjectBrowserWindow::AddMenuItem(const MenuItemCreation& menuItem)
	{
		menuItemContext.AddMenuItem(menuItem);
	}

	bool ProjectBrowserWindow::CanCreateAsset(const MenuItemEventData& eventData)
	{
		return true;
	}

	bool ProjectBrowserWindow::CheckSelectedAsset(const MenuItemEventData& eventData)
	{
		return true;
	}

	void ProjectBrowserWindow::AssetRename(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Rename");
		projectBrowserWindow->SetRenameItem(projectBrowserWindow->GetLastSelectedItem(), scope);
	}

	void ProjectBrowserWindow::AssetNewFolder(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Folder Creation");
		RID rid = ResourceAssets::CreateDirectory(projectBrowserWindow->GetOpenDirectory(), "New Folder", scope);
		projectBrowserWindow->SetRenameItem(rid,  scope);
	}

	void ProjectBrowserWindow::AssetNew(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Creation");
		RID newAsset = ResourceAssets::CreateAsset(projectBrowserWindow->GetOpenDirectory(), eventData.userData, "", scope);
		projectBrowserWindow->SetRenameItem(Resources::GetParent(newAsset), scope);
	}

	void ProjectBrowserWindow::AssetDelete(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		ResourceObject windowObject = Resources::Write(projectBrowserWindow->windowObjectRID);

		UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Delete");

		for (RID rid : windowObject.GetReferenceArray(ProjectBrowserWindowData::SelectedItems))
		{
			if (Resources::GetStorage(rid)->parentFieldIndex == ResourceAssetDirectory::DirectoryAsset)
			{
				Resources::Destroy(Resources::GetParent(rid), scope);
			}
			else
			{
				Resources::Destroy(rid, scope);
			}
		}
		windowObject.ClearReferenceArray(ProjectBrowserWindowData::SelectedItems);
		windowObject.Commit(scope);
	}

	void ProjectBrowserWindow::AssetShowInExplorer(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		ResourceObject windowObject = Resources::Read(projectBrowserWindow->windowObjectRID);

		if (windowObject.GetSubObjectSetCount(ProjectBrowserWindowData::SelectedItems) > 0)
		{
			windowObject.IterateSubObjectSet(ProjectBrowserWindowData::SelectedItems, false, [](RID selected)
			{
			    if (StringView absolutePath = ResourceAssets::GetAbsolutePath(selected); !absolutePath.Empty())
			    {
			        SDL_OpenURL(absolutePath.CStr());
			    }
				return true;
			});
		}
		else if (RID openDirectory = windowObject.GetReference(ProjectBrowserWindowData::OpenDirectory))
		{
			if (StringView absolutePath = ResourceAssets::GetAbsolutePath(ResourceAssets::GetAsset(openDirectory)); !absolutePath.Empty())
			{
				SDL_OpenURL(absolutePath.CStr());
			}
		}
	}

	bool ProjectBrowserWindow::CanReimportAsset(const MenuItemEventData& eventData)
	{
		return false;
	}

	void ProjectBrowserWindow::ReimportAsset(const MenuItemEventData& eventData)
	{

	}

	bool ProjectBrowserWindow::CanExtractAsset(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		if (RID lastSelected = projectBrowserWindow->GetLastSelectedItem())
		{
			if (ResourceAssetHandler* handler = ResourceAssets::GetAssetHandler(lastSelected))
			{
				return handler->CanExtractAsset(ResourceAssets::GetAsset(lastSelected));
			}
		}
		return false;
	}

	void ProjectBrowserWindow::ExtractAsset(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		if (RID lastSelected = projectBrowserWindow->GetLastSelectedItem())
		{
			if (ResourceAssetHandler* handler = ResourceAssets::GetAssetHandler(lastSelected))
			{
				RID asset = ResourceAssets::GetAsset(lastSelected);
				if (handler->CanExtractAsset(asset))
				{
					handler->ExtractAsset(projectBrowserWindow->GetOpenDirectory(), asset);
				}
			}
		}
	}

	void ProjectBrowserWindow::RegisterType(NativeReflectType<ProjectBrowserWindow>& type)
	{
		Event::Bind<OnDropFileCallback, &ProjectBrowserWindow::OnDropFile>();

		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Project Browser", .action = OpenProjectBrowser});

		AddMenuItem(MenuItemCreation{.itemName = "New Folder", .icon = ICON_FA_FOLDER, .priority = 0, .action = AssetNewFolder, .enable = CanCreateAsset});
		// AddMenuItem(MenuItemCreation{.itemName = "New Material", .icon = ICON_FA_PAINTBRUSH, .priority = 15, .action = AssetNew, .enable = CanCreateAsset, .userData = GetTypeID<MaterialAsset>()});
		AddMenuItem(MenuItemCreation{.itemName = "New Mesh", .icon = ICON_FA_PAINTBRUSH, .priority = 15, .action = AssetNew, .enable = CanCreateAsset, .userData = TypeInfo<MeshResource>::ID()});
		AddMenuItem(MenuItemCreation{.itemName = "Delete", .icon = ICON_FA_TRASH, .priority = 20, .itemShortcut{.presKey = Key::Delete}, .action = AssetDelete, .enable = CheckSelectedAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Rename", .icon = ICON_FA_PEN_TO_SQUARE, .priority = 30, .itemShortcut{.presKey = Key::F2}, .action = AssetRename, .enable = CheckSelectedAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Show in Explorer", .icon = ICON_FA_FOLDER, .priority = 40, .action = AssetShowInExplorer});
		// AddMenuItem(MenuItemCreation{.itemName = "Copy Path", .priority = 1000, .action = AssetCopyPathToClipboard});
		//AddMenuItem(MenuItemCreation{.itemName = "Create New Asset", .icon = ICON_FA_PLUS, .priority = 150});
		// AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Shader", .icon = ICON_FA_BRUSH, .priority = 10, .action = AssetNew, .menuData = (VoidPtr)GetTypeID<ShaderAsset>()});
		// AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Resource Graph", .icon = ICON_FA_DIAGRAM_PROJECT, .priority = 10, .action = AssetNewResourceGraph});
		// AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Behavior Graph", .icon = ICON_FA_DIAGRAM_PROJECT, .priority = 20});
		// AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Environment", .icon = ICON_FA_GLOBE, .priority = 10, .action = AssetNew, .userData = 0});

		AddMenuItem(MenuItemCreation{.itemName = "Reimport Asset", .icon = ICON_FA_UPLOAD, .priority = 300, .action = ReimportAsset, .enable = CanReimportAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Extract Assets", .icon = ICON_FA_EXPAND, .priority = 310, .action = ExtractAsset, .enable = CanExtractAsset});


		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::BottomLeft,
			.createOnInit = true
		});

		Resources::Type<ProjectBrowserWindowData>()
			.Field(ProjectBrowserWindowData::OpenDirectory, "openDirectory", ResourceFieldType::Reference)
			.Field(ProjectBrowserWindowData::RenamingItem, "renamingItem", ResourceFieldType::Reference)
			.Field(ProjectBrowserWindowData::SelectedItems, "selectedItems", ResourceFieldType::ReferenceArray)
			.Field(ProjectBrowserWindowData::LastSelectedItem, "lastSelectedItem", ResourceFieldType::Reference)
			.Build();
	}

	void ProjectBrowserWindowShutdown()
	{
		Graphics::WaitIdle();
		directoryTexture->Destroy();
		assertTexture->Destroy();
	}

	void ProjectBrowserWindowInit()
	{
		Event::Bind<OnShutdown, &ProjectBrowserWindowShutdown>();

		directoryTexture = StaticContent::GetTexture("Content/Images/FolderIcon.png");
		assertTexture = StaticContent::GetTexture("Content/Images/FileIcon.png");
	}
}
