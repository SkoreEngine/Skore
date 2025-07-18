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

#include "SceneViewWindow.hpp"
#include "SDL3/SDL.h"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Utils/StaticContent.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/Graphics/GraphicsResources.hpp"
#include "Skore/Resource/ResourceAssets.hpp"
#include "Skore/Resource/ResourceType.hpp"
#include "Skore/Scene/SceneCommon.hpp"


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
			ResourceAssets::ImportAsset(lastOpenedWindow->GetOpenDirectory(), {filePath});
		}
	}

	void ProjectBrowserWindow::DrawPathItems()
	{
		RID openDirectory = GetOpenDirectory();

		if (!openDirectory) return;

		directoryCache.Clear();
		{
			RID item = openDirectory;
			while (item)
			{
				directoryCache.EmplaceBack(item);
				if (RID parent = ResourceAssets::GetParentAsset(item))
				{
					if (Resources::GetStorage(parent)->resourceType->GetID() == TypeInfo<ResourceAssetDirectory>::ID())
					{
						item = parent;
						continue;
					}
				}
				item = {};
			}

			RID nextDirectory{};

			for (usize i = directoryCache.Size(); i > 0; --i)
			{
				RID drawItem = directoryCache[i - 1];

				String assetName = ResourceAssets::GetAssetName(drawItem);

				if (ImGui::Button(assetName.CStr()))
				{
					nextDirectory = drawItem;
				}
				if (i > 1)
				{
					bool openPopup = false;
					ImGui::PushID(IntToPtr(drawItem.id));
					if (ImGui::Button(ICON_FA_ARROW_RIGHT))
					{
						openPopup = true;
					}
					ImGui::PopID();

					if (openPopup)
					{
						popupFolder = drawItem;
						ImGui::OpenPopup("select-folder-browser-popup");
					}
				}
			}

			if (nextDirectory)
			{
				SetOpenDirectory(nextDirectory);
			}

			auto popupRes = ImGuiBeginPopupMenu("select-folder-browser-popup");
			if (popupRes && popupFolder)
			{
				if (ResourceObject openDirectoryObject = Resources::Read(popupFolder))
				{
					for (RID directory : openDirectoryObject.GetSubObjectList(ResourceAssetDirectory::Directories))
					{
						String assetName = ResourceAssets::GetAssetName(directory);
						if (ImGui::MenuItem(assetName.CStr()))
						{
							SetOpenDirectory(directory);
						}
					}
				}
			}
			ImGuiEndPopupMenu(popupRes);
		}
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
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD))
			{
				if (payload->IsDataType(SK_ASSET_PAYLOAD))
				{
					if (AssetPayload* assetPayload = static_cast<AssetPayload*>(ImGui::GetDragDropPayload()->Data))
					{
						if (ResourceObject originWindowObject = Resources::Read(assetPayload->windowObjectRID))
						{
							UndoRedoScope* scope = Editor::CreateUndoRedoScope("Move Assets");

							for (RID selected : originWindowObject.GetReferenceArray(ProjectBrowserWindowData::SelectedItems))
							{
								ResourceAssets::MoveAsset(asset, selected, scope);
							}
						}
					}
				}
			}

			ImGui::EndDragDropTarget();
		}


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
			Array<RID> children = directoryObject.GetSubObjectList(ResourceAssetDirectory::Directories);
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

		RID moveAssetsTo = {};
		RID moveOrigin = {};

		ResourceObject windowObject = Resources::Read(windowObjectRID);
		RID            openDirectory = windowObject.GetReference(ProjectBrowserWindowData::OpenDirectory);

		String labelCache = "";

		ImGuiStyle&    style = ImGui::GetStyle();
		ImVec2         pad = style.WindowPadding;
		bool           readOnly = false;
		ScopedStyleVar windowPadding(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ScopedStyleVar cellPadding(ImGuiStyleVar_CellPadding, ImVec2(0, 0));

		ScopedStyleColor tableBorderStyleColor(ImGuiCol_TableBorderLight, IM_COL32(0, 0, 0, 0));
		ImGuiBegin(id, ICON_FA_FOLDER " Project Browser", &open, ImGuiWindowFlags_NoScrollbar);

		//top child
		{
			ImVec2         a = ImVec2(pad.x / 1.5f, pad.y / 1.5f);
			ScopedStyleVar childPadding(ImGuiStyleVar_WindowPadding, a);
			f32            width = ImGui::GetContentRegionAvail().x - a.x;
			ImGui::BeginChild(id + 5, ImVec2(width, 30 * style.ScaleFactor), false, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoScrollbar);

			ImGui::BeginHorizontal((i32)id + 10, ImVec2(width - a.x - pad.x, 0));

			ImGui::BeginDisabled(readOnly);
			if (ImGui::Button(ICON_FA_PLUS " Import"))
			{
				SDL_ShowOpenFileDialog([](void* userdata, const char* const * filelist, int filter) {},
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
								if (!desc.selected)
								{
									UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Selection");

									if (!(ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_RightCtrl))))
									{
										ClearSelection(scope);
									}

									SelectItem(asset, scope);
								}
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

							ImGui::SetCursorScreenPos(ImVec2(state.screenStartPos.x + 3 * style.ScaleFactor, state.screenStartPos.y + 3 * style.ScaleFactor));
							ImGui::PushID(desc.id + 678);
							ImGui::InvisibleButton("", ImVec2(state.size.x - 7 * style.ScaleFactor, state.size.y - 6 * style.ScaleFactor));

							if (isDirectory && ImGui::BeginDragDropTarget())
							{
								if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD))
								{
									if (payload->IsDataType(SK_ASSET_PAYLOAD))
									{
										if (AssetPayload* assetPayload = static_cast<AssetPayload*>(ImGui::GetDragDropPayload()->Data))
										{
											moveAssetsTo = asset;
											moveOrigin = assetPayload->windowObjectRID;
										}
									}
								}
								ImGui::EndDragDropTarget();
							}

							if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
							{
								AssetPayload payload = {
									.asset = assetObject.GetSubObject(ResourceAsset::Object),
									.windowObjectRID = windowObjectRID
								};

								ImGui::SetDragDropPayload(SK_ASSET_PAYLOAD, &payload, sizeof(AssetPayload));
								ImGui::Text("%s", desc.label.CStr());
								ImGui::EndDragDropSource();
							}


							if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip())
							{
								// if (assetFile->GetStatus() != AssetStatus::None)
								// {
								// 	ImGui::TextColored(ImVec4(202.f / 255.f, 98.f / 255.f, 87.f / 255.f, 1.0f), "WARNING! asset is in an invalid state. ");
								//
								// 	for (const String& missingFile : assetFile->GetMissingFiles())
								// 	{
								// 		ImGui::TextColored(ImVec4(202.f / 255.f, 98.f / 255.f, 87.f / 255.f, 1.0f), "File %s not found. ", missingFile.CStr());
								// 	}
								//
								// 	ImGui::Separator();
								// 	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 16.f * style.ScaleFactor);
								// }

								if (ImGui::BeginTable("table-asset-info", 2, ImGuiTableFlags_SizingFixedFit))
								{
									RID object = assetObject.GetSubObject(ResourceAsset::Object);
									if (UUID uuid = Resources::GetUUID(object))
									{
										ImGui::TableNextRow();
										ImGui::TableNextColumn();
										ImGui::TextDisabled("UUID: ");
										ImGui::TableNextColumn();
										ImGui::Text("%s", uuid.ToString().CStr());
									}

									ImGui::TableNextRow();
									ImGui::TableNextColumn();
									ImGui::TextDisabled("Asset Name: ");
									ImGui::TableNextColumn();
									ImGui::Text("%s", labelCache.CStr());

									ImGui::TableNextRow();
									ImGui::TableNextColumn();
									ImGui::TextDisabled("Path Id: ");
									ImGui::TableNextColumn();
									ImGui::Text("%s", ResourceAssets::GetPathId(asset).CStr());

									ImGui::TableNextRow();
									ImGui::TableNextColumn();
									ImGui::TextDisabled("Absolute Path: ");
									ImGui::TableNextColumn();
									ImGui::Text("%s", ResourceAssets::GetAbsolutePath(asset).CStr());


									//TODO : get from the handler
									if (ResourceType* type = Resources::GetType(object))
									{
										ImGui::TableNextRow();
										ImGui::TableNextColumn();
										ImGui::TextDisabled("Type: ");
										ImGui::TableNextColumn();
										ImGui::Text("%s", type->GetName().CStr());
									}

									if (Editor::DebugOptionsEnabled())
									{
										u64 currentVersion = 0;
										u64 persistedVersion = 0;
										if (ResourceAssets::GetAssetVersions(asset, currentVersion, persistedVersion))
										{
											ImGui::TableNextRow();
											ImGui::TableNextColumn();
											ImGui::TextDisabled("(Debug) Version: ");
											ImGui::TableNextColumn();
											ImGui::Text("%u", currentVersion);

											ImGui::TableNextRow();
											ImGui::TableNextColumn();
											ImGui::TextDisabled("(Debug) Persisted Version: ");
											ImGui::TableNextColumn();
											ImGui::Text("%u", persistedVersion);
										}
									}

									ImGui::EndTable();
								}
								ImGui::EndTooltip();
							}

							ImGui::SetCursorScreenPos(state.screenStartPos);
							ImGui::PopID();

							return state;
						};
						ResourceObject openDirectoryObject = Resources::Read(openDirectory);

						for (RID directory : openDirectoryObject.GetSubObjectList(ResourceAssetDirectory::Directories))
						{
							ResourceObject        directoryObject = Resources::Read(directory);
							RID                   asset = directoryObject.GetSubObject(ResourceAssetDirectory::DirectoryAsset);
							ImGuiContentItemState state = drawContentItem(asset, true);
							if (state.enter)
							{
								newOpenDirectory = directory;
								ClearSelection(nullptr);
							}
						}

						for (RID asset : openDirectoryObject.GetSubObjectList(ResourceAssetDirectory::Assets))
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

				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ENTITY_PAYLOAD))
					{
						UndoRedoScope* scope = Editor::CreateUndoRedoScope("Create Entity Assat");
						Span<RID> selected = Editor::GetCurrentWorkspace().GetSceneEditor()->GetSelectedEntities();
						for (RID entity : selected)
						{
							if (ResourceObject entityObject = Resources::Read(entity))
							{
								ResourceAssets::DuplicateAsset(openDirectory, entity, entityObject.GetString(EntityResource::Name), scope);
							}
						}
					}
				}
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

		if (moveAssetsTo && moveOrigin)
		{
			if (ResourceObject originWindowObject = Resources::Read(moveOrigin))
			{
				UndoRedoScope* scope = Editor::CreateUndoRedoScope("Move Assets");

				for (RID rid : originWindowObject.GetReferenceArray(ProjectBrowserWindowData::SelectedItems))
				{
					ResourceAssets::MoveAsset(moveAssetsTo, rid, scope);
				}
			}
		}

		if (!popupRes && !newSelection && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		{
			ClearSelection(nullptr);
		}

		newSelection = false;

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
		{
			RID parent = Resources::GetParent(GetOpenDirectory());
			if (parent && Resources::GetType(parent)->GetID() == TypeInfo<ResourceAssetDirectory>::ID())
			{
				SetOpenDirectory(parent);
			}
		}

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
		ClearSelection(scope);

		ResourceObject windowObject = Resources::Write(windowObjectRID);
		windowObject.SetReference(ProjectBrowserWindowData::RenamingItem, rid);
		windowObject.Commit(scope);

		SelectItem(rid, scope);
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
		UndoRedoScope*        scope = Editor::CreateUndoRedoScope("Asset Rename");
		projectBrowserWindow->SetRenameItem(projectBrowserWindow->GetLastSelectedItem(), scope);
	}

	void ProjectBrowserWindow::AssetNewFolder(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		UndoRedoScope*        scope = Editor::CreateUndoRedoScope("Folder Creation");
		RID                   rid = ResourceAssets::CreateDirectory(projectBrowserWindow->GetOpenDirectory(), "New Folder", scope);
		projectBrowserWindow->SetRenameItem(rid, scope);
	}

	void ProjectBrowserWindow::AssetNew(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		UndoRedoScope*        scope = Editor::CreateUndoRedoScope("Asset Creation");
		RID                   newAsset = ResourceAssets::CreateAsset(projectBrowserWindow->GetOpenDirectory(), eventData.userData, "", scope);

		RID newAssetParent = Resources::GetParent(newAsset);
		projectBrowserWindow->SetRenameItem(newAssetParent, scope);
	}

	void ProjectBrowserWindow::AssetDelete(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		ResourceObject        windowObject = Resources::Write(projectBrowserWindow->windowObjectRID);

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
		ResourceObject        windowObject = Resources::Read(projectBrowserWindow->windowObjectRID);

		if (RID openDirectory = windowObject.GetReference(ProjectBrowserWindowData::OpenDirectory))
		{
			if (StringView absolutePath = ResourceAssets::GetAbsolutePath(ResourceAssets::GetAsset(openDirectory)); !absolutePath.Empty())
			{
				SDL_OpenURL(absolutePath.CStr());
			}
		}
	}

	void ProjectBrowserWindow::AssetCopyPathIdToClipboard(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		if (RID lastSelected = projectBrowserWindow->GetLastSelectedItem())
		{
			SDL_SetClipboardText(ResourceAssets::GetPathId(lastSelected).CStr());
		}
	}

	bool ProjectBrowserWindow::CanReimportAsset(const MenuItemEventData& eventData)
	{
		return false;
	}

	void ProjectBrowserWindow::ReimportAsset(const MenuItemEventData& eventData) {}

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

	bool ProjectBrowserWindow::CanCreateInherited(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		if (RID lastSelected = projectBrowserWindow->GetLastSelectedItem())
		{
			if (ResourceAssetHandler* handler = ResourceAssets::GetAssetHandler(lastSelected))
			{
				RID asset = ResourceAssets::GetAsset(lastSelected);
				return handler->CanInherit(asset);
			}
		}
		return false;
	}

	void ProjectBrowserWindow::CreateInherited(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		UndoRedoScope*        scope = Editor::CreateUndoRedoScope("Asset Creation");

		if (RID lastSelected = projectBrowserWindow->GetLastSelectedItem())
		{
			if (ResourceAssetHandler* handler = ResourceAssets::GetAssetHandler(lastSelected))
			{
				RID asset = ResourceAssets::GetAsset(lastSelected);
				RID newAsset = ResourceAssets::CreateInheritedAsset(projectBrowserWindow->GetOpenDirectory(), asset, "", scope);
				projectBrowserWindow->ClearSelection(scope);
				projectBrowserWindow->SetRenameItem(Resources::GetParent(newAsset), scope);
			}
		}
	}

	void ProjectBrowserWindow::RegisterType(NativeReflectType<ProjectBrowserWindow>& type)
	{
		Event::Bind<OnDropFileCallback, &ProjectBrowserWindow::OnDropFile>();

		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Project Browser", .action = OpenProjectBrowser});


		//TODO: find a better icon
		AddMenuItem(MenuItemCreation{.itemName = "Create Inherited Asset", .icon = ICON_FA_ENVELOPE, .priority = -100, .action = CreateInherited, .visible = CanCreateInherited});

		AddMenuItem(MenuItemCreation{.itemName = "New Folder", .icon = ICON_FA_FOLDER, .priority = 5, .action = AssetNewFolder, .enable = CanCreateAsset});
		// AddMenuItem(MenuItemCreation{.itemName = "New Script", .icon = ICON_FA_SCROLL, .priority = 5, .action = AssetNew, .enable = CanCreateAsset, .userData = TypeInfo<PkPyScriptResource>::ID()});
		AddMenuItem(MenuItemCreation{.itemName = "New Material", .icon = ICON_FA_PAINTBRUSH, .priority = 15, .action = AssetNew, .enable = CanCreateAsset, .userData = TypeInfo<MaterialResource>::ID()});
		AddMenuItem(MenuItemCreation{.itemName = "Delete", .icon = ICON_FA_TRASH, .priority = 20, .itemShortcut{.presKey = Key::Delete}, .action = AssetDelete, .enable = CheckSelectedAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Rename", .icon = ICON_FA_PEN_TO_SQUARE, .priority = 30, .itemShortcut{.presKey = Key::F2}, .action = AssetRename, .enable = CheckSelectedAsset});

		AddMenuItem(MenuItemCreation{.itemName = "Show in Explorer", .icon = ICON_FA_FOLDER, .priority = 240, .action = AssetShowInExplorer});
		AddMenuItem(MenuItemCreation{.itemName = "Copy Path Id", .icon = ICON_FA_COPY, .priority = 250, .action = AssetCopyPathIdToClipboard});


		//AddMenuItem(MenuItemCreation{.itemName = "Create New Asset", .icon = ICON_FA_PLUS, .priority = 150});
		// AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Shader", .icon = ICON_FA_BRUSH, .priority = 10, .action = AssetNew, .menuData = (VoidPtr)GetTypeID<ShaderAsset>()});
		// AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Resource Graph", .icon = ICON_FA_DIAGRAM_PROJECT, .priority = 10, .action = AssetNewResourceGraph});
		// AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Behavior Graph", .icon = ICON_FA_DIAGRAM_PROJECT, .priority = 20});
		// AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Environment", .icon = ICON_FA_GLOBE, .priority = 10, .action = AssetNew, .userData = 0});



		AddMenuItem(MenuItemCreation{.itemName = "Reimport Asset", .icon = ICON_FA_UPLOAD, .priority = 1000, .action = ReimportAsset, .enable = CanReimportAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Extract Assets", .icon = ICON_FA_EXPAND, .priority = 1010, .action = ExtractAsset, .enable = CanExtractAsset});


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
