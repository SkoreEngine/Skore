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

#include <regex>

#include "Skore/App.hpp"
#include "Skore/Editor.hpp"
#include "Skore/Asset/AssetEditor.hpp"
#include "Skore/Asset/AssetFile.hpp"
#include "Skore/Asset/AssetTypes.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/IconsFontAwesome6.h"
#include "Skore/ImGui/ImGui.hpp"
#include "Skore/Scene/Scene.hpp"

#include "SDL3/SDL.h"
#include "Skore/Events.hpp"
#include "Skore/Graphics/GraphicsAssets.hpp"
#include "Skore/IO/Path.hpp"

namespace Skore
{
	SDL_Window* GraphicsGetWindow();

	MenuItemContext ProjectBrowserWindow::menuItemContext = {};

	ProjectBrowserWindow::ProjectBrowserWindow()
	{
		Event::Bind<OnDropFileCallback, &ProjectBrowserWindow::OnDropFile>(this);
	}

	ProjectBrowserWindow::~ProjectBrowserWindow()
	{
		Event::Unbind<OnDropFileCallback, &ProjectBrowserWindow::OnDropFile>(this);
	}

	void ProjectBrowserWindow::Init(u32 id, VoidPtr userData)
	{
		openDirectory = AssetEditor::GetProject();
	}

	void ProjectBrowserWindow::OnDropFile(StringView filePath) const
	{
		if (openDirectory != nullptr)
		{
			AssetEditor::ImportFile(openDirectory, filePath);
		}
	}

	void ProjectBrowserWindow::DrawPathItems()
	{
		//TODO
	}

	void ProjectBrowserWindow::DrawTreeNode(AssetFile* file)
	{
		if (!file || !file->IsDirectory()) return;

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;

		bool openDir = openTreeFolders.Has(file->GetAbsolutePath());


		if (!openDir && openDirectory != nullptr && openDirectory->IsChildOf(file))
		{
			openTreeFolders[file->GetAbsolutePath()] = true;
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

		stringCache.Append(" ").Append(file->GetFileName());
		bool isNodeOpen = ImGuiTreeNode(file, stringCache.CStr(), flags);

		if (ImGui::BeginDragDropTarget())
		{
			if (file->CanAcceptNewChild())
			{
				bool canAcceptDragDrop = false;

				for (auto& it : selectedItems)
				{
					if (!file->IsChildOf(it))
					{
						canAcceptDragDrop = true;
						break;
					}
				}

				if (canAcceptDragDrop && ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD))
				{
					for (auto& it : selectedItems)
					{
						if (!file->IsChildOf(it))
						{
							it->MoveTo(file);
						}
					}
				}
			}

			ImGui::EndDragDropTarget();
		}

		if (file->GetParent() != nullptr && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover | ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
		{
			AssetPayload payload = {
				.assetFile = file,
				.assetType = file->GetHandler() != nullptr ? file->GetHandler()->GetAssetTypeId() : 0
			};
			ImGui::SetDragDropPayload(SK_ASSET_PAYLOAD, &payload, sizeof(AssetPayload));

			ImGui::Text("%s", file->GetFileName().CStr());
			ImGui::EndDragDropSource();
		}

		if (openDir == isNodeOpen && ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			SetOpenDirectory(file);
		}

		openTreeFolders[file->GetAbsolutePath()] = isNodeOpen;

		if (isNodeOpen)
		{
			for (AssetFile* childNode : file->Children())
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
				SDL_ShowOpenFileDialog([](void *userdata, const char * const *filelist, int filter)
				{

				},
				nullptr, GraphicsGetWindow(), nullptr, 0, nullptr, true);

				// Array<String>     paths{};
				// Array<FileFilter> filters;
				// //assetEditor.FilterExtensions(filters);
				//
				// if (Platform::OpenDialogMultiple(paths, filters, {}) == DialogResult::OK)
				// {
				// 	if (!paths.Empty())
				// 	{
				// 		AssetEditor::ImportAssets(openDirectory, paths);
				// 	}
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

				for (const auto& package : AssetEditor::GetPackages())
				{
					DrawTreeNode(package);
				}

				DrawTreeNode(AssetEditor::GetProject());

				ImGuiEndTreeNodeStyle();
				ImGui::EndChild();
			}

			//TODO std::regex on loop = shit
			auto rx = std::regex{searchString.CStr(), std::regex_constants::icase};

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
					AssetFile* newOpenDirectory = nullptr;

					AssetFile* moveAssetsTo = nullptr;

					if (openDirectory != nullptr)
					{
						for (int i = 0; i < 2; ++i)
						{
							for (AssetFile* assetFile : openDirectory->Children())
							{
								if (!assetFile->IsActive()) continue;

								//workaround to show directories first.
								if (i == 0 && !assetFile->IsDirectory()) continue;
								if (i == 1 && assetFile->IsDirectory()) continue;


								//TODO slow!!!! cache it on AssetEditor.
								if (!searchString.Empty())
								{
									if (!std::regex_search(assetFile->GetFileName().CStr(), rx))
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

								labelCache += assetFile->GetFileName();

								if (!renaming)
								{
									labelCache += assetFile->GetExtension();
								}

								ImGuiContentItemDesc desc;
								desc.id = reinterpret_cast<usize>(assetFile);
								desc.label = labelCache.CStr();
								desc.texture = assetFile->GetThumbnail();
								desc.renameItem = renaming;
								desc.thumbnailScale = contentBrowserZoom;
								desc.selected = selectedItems.Has(assetFile);
								desc.showError = assetFile->GetStatus() != AssetStatus::None;

								ImGuiContentItemState state = ImGuiContentItem(desc);

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

								if (state.enter)
								{
									if (assetFile->IsDirectory())
									{
										newOpenDirectory = assetFile;
										selectedItems.Clear();
										lastSelectedItem = nullptr;
									}
									else if (assetFile->GetHandler())
									{
										assetFile->GetHandler()->OpenAsset(assetFile);
									} else
									{
										SDL_OpenURL(assetFile->GetAbsolutePath().CStr());
									}
								}

								if (state.renameFinish)
								{
									if (!state.newName.Empty())
									{
										assetFile->Rename(state.newName);
									}
									renamingItem = nullptr;
								}

								ImGui::SetCursorScreenPos(ImVec2(state.screenStartPos.x + 3 * style.ScaleFactor, state.screenStartPos.y + 3 * style.ScaleFactor));
								ImGui::PushID(desc.id + 678);
								ImGui::InvisibleButton("", ImVec2(state.size.x - 7 * style.ScaleFactor, state.size.y - 6 * style.ScaleFactor));

								if (assetFile->IsDirectory() && ImGui::BeginDragDropTarget())
								{
									if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(SK_ASSET_PAYLOAD))
									{
										moveAssetsTo = assetFile;
									}
									ImGui::EndDragDropTarget();
								}

								if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
								{
									AssetPayload payload = {
										.assetFile = assetFile,
										.assetType = assetFile->GetHandler() != nullptr ? assetFile->GetHandler()->GetAssetTypeId() : 0
									};

									ImGui::SetDragDropPayload(SK_ASSET_PAYLOAD, &payload, sizeof(AssetPayload));
									ImGui::Text("%s", desc.label.CStr());
									ImGui::EndDragDropSource();
								}

								if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip())
								{
									if (assetFile->GetStatus() != AssetStatus::None)
									{
										ImGui::TextColored(ImVec4(202.f / 255.f, 98.f / 255.f, 87.f / 255.f, 1.0f), "WARNING! asset is in an invalid state. ");

										for (const String& missingFile: assetFile->GetMissingFiles())
										{
											ImGui::TextColored(ImVec4(202.f / 255.f, 98.f / 255.f, 87.f / 255.f, 1.0f), "File %s not found. ", missingFile.CStr());
										}

										ImGui::Separator();
										ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 16.f * style.ScaleFactor);
									}

									if (ImGui::BeginTable("table-asset-info", 2, ImGuiTableFlags_SizingFixedFit))
									{
										if (assetFile->GetUUID())
										{
											ImGui::TableNextRow();
											ImGui::TableNextColumn();
											ImGui::TextDisabled("UUID: ");
											ImGui::TableNextColumn();
											ImGui::Text("%s", assetFile->GetUUID().ToString().CStr());
										}

										ImGui::TableNextRow();
										ImGui::TableNextColumn();
										ImGui::TextDisabled("Asset Name: ");
										ImGui::TableNextColumn();
										ImGui::Text("%s", labelCache.CStr());

										ImGui::TableNextRow();
										ImGui::TableNextColumn();
										ImGui::TextDisabled("Relative Path: ");
										ImGui::TableNextColumn();
										ImGui::Text("%s", assetFile->GetPath().CStr());

										ImGui::TableNextRow();
										ImGui::TableNextColumn();
										ImGui::TextDisabled("Absolute Path: ");
										ImGui::TableNextColumn();
										ImGui::Text("%s", assetFile->GetAbsolutePath().CStr());

										if (ReflectType* type = Reflection::FindTypeById(assetFile->GetAssetTypeId()))
										{
											ImGui::TableNextRow();
											ImGui::TableNextColumn();
											ImGui::TextDisabled("Type: ");
											ImGui::TableNextColumn();
											ImGui::Text("%s", type->GetSimpleName().CStr());
										}

										ImGui::EndTable();
									}
									ImGui::EndTooltip();
								}

								ImGui::SetCursorScreenPos(state.screenStartPos);

								ImGui::PopID();
							}
						}
					}

					ImGuiEndContentTable();

					if (newOpenDirectory)
					{
						SetOpenDirectory(newOpenDirectory);
					}

					if (moveAssetsTo)
					{
						for (auto& it : selectedItems)
						{
							it->MoveTo(moveAssetsTo);
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
			selectedItems.Clear();
			lastSelectedItem = nullptr;
		}

		newSelection = false;

		ImGui::End();
	}

	AssetFile* ProjectBrowserWindow::GetLastSelectedItem() const
	{
		return lastSelectedItem;
	}

	AssetFile* ProjectBrowserWindow::GetOpenDirectory() const
	{
		return openDirectory;
	}

	void ProjectBrowserWindow::SetOpenDirectory(AssetFile* directory)
	{
		selectedItems.Clear();
		selectedItems.Emplace(directory);
		lastSelectedItem = directory;
		newSelection = true;

		openDirectory = directory;
		if (directory && directory->GetParent())
		{
			openTreeFolders[directory->GetParent()->GetAbsolutePath()] = true;
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
		projectBrowserWindow->markedToDelete = projectBrowserWindow->selectedItems;

		Editor::ShowConfirmDialog("Are you sure you want to delete the selected assets? ", eventData.drawData, [](VoidPtr userData)
		{
			ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(userData);

			for (AssetFile* assetFile : projectBrowserWindow->markedToDelete)
			{
				assetFile->Delete();
			}

			projectBrowserWindow->markedToDelete.Clear();
			projectBrowserWindow->selectedItems.Clear();
			projectBrowserWindow->lastSelectedItem = nullptr;
		});
	}

	void ProjectBrowserWindow::AssetShowInExplorer(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		if (projectBrowserWindow->lastSelectedItem)
		{
			String parent = Path::Parent(projectBrowserWindow->lastSelectedItem->GetAbsolutePath());
			SDL_OpenURL(parent.CStr());
		}
		else if (projectBrowserWindow->openDirectory)
		{
			SDL_OpenURL(projectBrowserWindow->openDirectory->GetAbsolutePath().CStr());
		}
	}

	void ProjectBrowserWindow::AssetCopyPathToClipboard(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		if (projectBrowserWindow->lastSelectedItem)
		{
			SDL_SetClipboardText(projectBrowserWindow->lastSelectedItem->GetPath().CStr());
		}
	}

	bool ProjectBrowserWindow::CanCreateAsset(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		return projectBrowserWindow->openDirectory != nullptr && projectBrowserWindow->openDirectory->CanAcceptNewChild();
	}

	void ProjectBrowserWindow::OpenProjectBrowser(const MenuItemEventData& eventData)
	{
		Editor::OpenWindow(TypeInfo<ProjectBrowserWindow>::ID());
	}

	void ProjectBrowserWindow::AddMenuItem(const MenuItemCreation& menuItem)
	{
		menuItemContext.AddMenuItem(menuItem);
	}

	bool ProjectBrowserWindow::CanReimportAsset(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		if (projectBrowserWindow->lastSelectedItem)
		{
			return projectBrowserWindow->lastSelectedItem->GetAssetTypeFile() == AssetFileType::ImportedAsset;
		}
		return false;
	}

	void ProjectBrowserWindow::ReimportAsset(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		if (projectBrowserWindow->lastSelectedItem)
		{
			projectBrowserWindow->lastSelectedItem->Reimport();
		}
	}

	bool ProjectBrowserWindow::CanExtractAsset(const MenuItemEventData& eventData)
	{
		return false;
	}

	void ProjectBrowserWindow::ExtractAsset(const MenuItemEventData& eventData)
	{

	}

	void ProjectBrowserWindow::RegisterType(NativeReflectType<ProjectBrowserWindow>& type)
	{
		Event::Bind<OnShutdown, Shutdown>();

		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Project Browser", .action = OpenProjectBrowser});

		AddMenuItem(MenuItemCreation{.itemName = "New Folder", .icon = ICON_FA_FOLDER, .priority = 1000, .action = AssetNewFolder, .enable = CanCreateAsset});
		AddMenuItem(MenuItemCreation{.itemName = "New Scene", .icon = ICON_FA_CLAPPERBOARD, .priority = 1010, .action = AssetNew, .enable = CanCreateAsset, .userData = TypeInfo<Scene>::ID()});
		AddMenuItem(MenuItemCreation{.itemName = "New Material", .icon = ICON_FA_PAINTBRUSH, .priority = 1015, .action = AssetNew, .enable = CanCreateAsset, .userData = TypeInfo<MaterialAsset>::ID()});
		AddMenuItem(MenuItemCreation{.itemName = "Delete", .icon = ICON_FA_TRASH, .priority = 1020, .itemShortcut{.presKey = Key::Delete}, .action = AssetDelete, .visible = CheckSelectedAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Rename", .icon = ICON_FA_PEN_TO_SQUARE, .priority = 1030, .itemShortcut{.presKey = Key::F2}, .action = AssetRename, .visible = CheckSelectedAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Show in Explorer", .icon = ICON_FA_FOLDER, .priority = 1040, .action = AssetShowInExplorer});
		//AddMenuItem(MenuItemCreation{.itemName = "Reimport", .icon = ICON_FA_REPEAT, .priority = 50, .action = AssetReimport, .enable = CheckCanReimport});
		//AddMenuItem(MenuItemCreation{.itemName = "Copy Path", .priority = 1000, .action = AssetCopyPathToClipboard});
		//AddMenuItem(MenuItemCreation{.itemName = "Create New Asset", .icon = ICON_FA_PLUS, .priority = 150});
		// AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Shader", .icon = ICON_FA_BRUSH, .priority = 10, .action = AssetNew, .menuData = (VoidPtr)TypeInfo<<ShaderAsset>()});
		// AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Resource Graph", .icon = ICON_FA_DIAGRAM_PROJECT, .priority = 10, .action = AssetNewResourceGraph});
		// AddMenuItem(MenuItemCreation{.itemName = "Create New Asset/Behavior Graph", .icon = ICON_FA_DIAGRAM_PROJECT, .priority = 20});

		AddMenuItem(MenuItemCreation{.itemName = "Reimport Asset", .icon = ICON_FA_UPLOAD, .priority = 1400, .action = ReimportAsset, .visible = CanReimportAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Extract Assets", .icon = ICON_FA_EXPAND, .priority = 1410, .action = ExtractAsset, .visible = CanExtractAsset});


		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::BottomLeft,
			.createOnInit = true
		});
	}
}
