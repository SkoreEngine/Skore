#include "Skore/Window/ProjectBrowserWindow.hpp"

#include "Skore/Window/ResourceDebuggerWindow.hpp"
#include "Skore/Window/SceneViewWindow.hpp"
#include "Skore/Editor.hpp"
#include "Skore/EditorWorkspace.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/ImGui/Icons.h"
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
		HashSet<String>       hiddenExtensions;
	}


	MenuItemContext ProjectBrowserWindow::menuItemContext = {};

	GPUTexture* directoryTexture;

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

		bool folderSelected = windowObject.HasOnReferenceArray(ProjectBrowserWindowData::SelectedItems, asset);
		if (folderSelected)
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

		bool readOnly = assetObject.GetBool(ResourceAsset::ReadOnly);

		RID  renamingItem = windowObject.GetReference(ProjectBrowserWindowData::RenamingItem);
		bool renamingHere = renamingItem == asset && ShouldRenameInTree();

		if (renamingHere)
		{
			ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
			DrawTreeRenameInput(asset, assetObject.GetString(ResourceAsset::Name));
			ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
			return;
		}

		stringCache.Append(" ");
		stringCache.Append(assetObject.GetString(ResourceAsset::Name));
		stringCache.Append(assetObject.GetString(ResourceAsset::Extension));

		bool isNodeOpen = ImGuiTreeNode(reinterpret_cast<void*>((usize)asset.id), stringCache.CStr(), flags);

		if (!readOnly && ImGui::BeginDragDropTarget())
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

		if (openDir == isNodeOpen && (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)))
		{
			if (!folderSelected)
			{
				UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Selection");
				if (!(ImGui::IsKeyDown((ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown((ImGuiKey_RightCtrl))))
				{
					ClearSelection(scope);
				}
				SelectItem(asset, scope);
			}
			newSelection = true;
			selectionInTree = true;

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				SetOpenDirectory(rid);
			}
		}

		openTreeFolders[asset] = isNodeOpen;

		if (isNodeOpen)
		{
			for (RID child : directoryObject.GetSubObjectList(ResourceAssetDirectory::Directories))
			{
				DrawDirectoryTreeNode(child);
			}

			if (treeOnlyView)
			{
				for (RID childAsset : directoryObject.GetSubObjectList(ResourceAssetDirectory::Assets))
				{
					DrawAssetTreeLeaf(childAsset);
				}
			}

			ImGui::TreePop();
		}
	}

	void ProjectBrowserWindow::DrawAssetTreeLeaf(RID asset)
	{
		if (!asset) return;

		ResourceObject assetObject = Resources::Read(asset);
		StringView     extension = assetObject.GetString(ResourceAsset::Extension);
		if (hiddenExtensions.Has(extension)) return;

		ResourceObject windowObject = Resources::Read(windowObjectRID);

		RID  renamingItem = windowObject.GetReference(ProjectBrowserWindowData::RenamingItem);
		bool renamingHere = renamingItem == asset && ShouldRenameInTree();

		if (renamingHere)
		{
			ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
			DrawTreeRenameInput(asset, assetObject.GetString(ResourceAsset::Name));
			ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
			return;
		}

		stringCache.Clear();
		if (const char* icon = ResourceAssets::GetIcon(asset); icon && icon[0])
		{
			stringCache = icon;
			stringCache.Append(" ");
		}
		stringCache.Append(assetObject.GetString(ResourceAsset::Name));
		stringCache.Append(extension);

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
		bool selected = windowObject.HasOnReferenceArray(ProjectBrowserWindowData::SelectedItems, asset);
		if (selected) flags |= ImGuiTreeNodeFlags_Selected;

		ImGuiTreeLeaf(reinterpret_cast<void*>((usize)asset.id), stringCache.CStr(), flags);

		bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

		if (isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			ResourceAssets::OpenAsset(asset);
		}
		else if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
		{
			if (!selected)
			{
				UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Selection");
				if (!(ImGui::IsKeyDown((ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown((ImGuiKey_RightCtrl))))
				{
					ClearSelection(scope);
				}
				SelectItem(asset, scope);
			}
			newSelection = true;
			selectionInTree = true;
		}

		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
		{
			AssetPayload payload = {
				.asset = assetObject.GetSubObject(ResourceAsset::Object),
				.windowObjectRID = windowObjectRID
			};

			ImGui::SetDragDropPayload(SK_ASSET_PAYLOAD, &payload, sizeof(AssetPayload));
			ImGui::Text("%s", stringCache.CStr());
			ImGui::EndDragDropSource();
		}
	}

	bool ProjectBrowserWindow::ShouldRenameInTree() const
	{
		return treeOnlyView || selectionInTree;
	}

	bool ProjectBrowserWindow::DrawTreeRenameInput(RID asset, const String& currentName)
	{
		bool firstFrame = !renameFocusSet;

		if (firstFrame)
		{
			renameBuffer = currentName;
			ImGui::SetKeyboardFocusHere();
		}

		ScopedStyleColor frameColor(ImGuiCol_FrameBg, IM_COL32(52, 53, 55, 255));
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGuiInputText((u32)asset.id, renameBuffer);

		if (firstFrame)
		{
			renameFocusSet = true;
			return false;
		}

		if (!ImGui::IsItemActive())
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Rename Finished");
			if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && !renameBuffer.Empty())
			{
				ResourceObject write = Resources::Write(asset);
				write.SetString(ResourceAsset::Name, renameBuffer);
				write.Commit(scope);
			}
			ResourceObject objWrite = Resources::Write(windowObjectRID);
			objWrite.SetReference(ProjectBrowserWindowData::RenamingItem, {});
			objWrite.Commit(scope);
			renameFocusSet = false;
			return true;
		}
		return false;
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

	const char* ProjectBrowserWindow::GetTitle() const
	{
		return ICON_FA_FOLDER " Project Browser";
	}

	void ProjectBrowserWindow::Init(VoidPtr userData)
	{
		ResourceObject packageProject = Resources::Read(Editor::GetProject());
		windowObjectRID = Resources::Create<ProjectBrowserWindowData>();
		ResourceObject obj = Resources::Write(windowObjectRID);
		obj.SetReference(ProjectBrowserWindowData::OpenDirectory, packageProject.GetSubObject(ResourceAssetPackage::Root));
		obj.Commit();
	}

	void ProjectBrowserWindow::Draw(bool& open)
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
		ImGuiBegin(this, &open, ImGuiWindowFlags_NoScrollbar);

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
				auto func = [openDirectory](Span<StringView> paths)
				{
					for (StringView path : paths)
					{
						ResourceAssets::ImportAsset(openDirectory, path);
					}
				};

				Platform::OpenDialogMultiple(func, {}, "", Graphics::GetWindow());
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
			if (ImGuiSearchInputText(id + 20, searchString))
			{
				cachedDirectory = {};
				memset(filter.InputBuf, 0, sizeof(filter.InputBuf));
				memcpy(filter.InputBuf, searchString.CStr(), Math::Min(searchString.Size(), sizeof(filter.InputBuf)));
				filter.Build();
			}


			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 0.f, 0.f, 0.f));

			if (ImGui::Button(ICON_FA_GEAR " Settings"))
			{
				ImGui::OpenPopup("project-browser-settings-popup");
			}

			ImGui::PopStyleColor(2);

			auto settingsPopupRes = ImGuiBeginPopupMenu("project-browser-settings-popup");
			if (settingsPopupRes)
			{
				bool splitView = !treeOnlyView;
				if (ImGui::MenuItem("Two Columns (Tree + Content)", nullptr, splitView))
				{
					treeOnlyView = false;
					cachedDirectory = {};
				}
				if (ImGui::MenuItem("One Column (Tree Only)", nullptr, treeOnlyView))
				{
					treeOnlyView = true;
				}
			}
			ImGuiEndPopupMenu(settingsPopupRes);

			ImGui::EndHorizontal();

			ImGui::EndChild();
		}

		auto* drawList = ImGui::GetWindowDrawList();

		auto p1 = ImGui::GetCursorScreenPos();
		auto p2 = ImVec2(ImGui::GetContentRegionAvail().x + p1.x, p1.y);
		drawList->AddLine(p1, p2, IM_COL32(0, 0, 0, 255), 1.f * style.ScaleFactor);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.f * style.ScaleFactor);

		bool browseFolder = !treeOnlyView;

		static ImGuiTableFlags flags = ImGuiTableFlags_Resizable;

		const char* tableId = browseFolder ? "table-project-browser-split" : "table-project-browser-tree";
		if (ImGui::BeginTable(tableId, browseFolder ? 2 : 1, flags))
		{
			if (browseFolder)
			{
				ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthFixed, 300 * style.ScaleFactor);
			}
			else
			{
				ImGui::TableSetupColumn("one", ImGuiTableColumnFlags_WidthStretch);
			}
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

				drawPackage(Editor::GetProject());

				{
					// bool isNodeOpen = ImGuiTreeNode(IntToPtr(123234), ICON_FA_CODE " Scripts", 0);
					// if (isNodeOpen)
					// {
					// 	ImGui::TreePop();
					// }
				}

				for (const auto& package : Editor::GetPackages())
				{
					drawPackage(package);
				}

				ImGuiEndTreeNodeStyle();
				ImGui::EndChild();
			}

			if (browseFolder)
			{
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

						ResourceObject openDirectoryObject = Resources::Read(openDirectory);

						if (openDirectory != cachedDirectory || Resources::GetVersion(openDirectory) != cachedDirectoryVersion)
						{
							visibleItems.Clear();
							cachedDirectory = openDirectory;
							cachedDirectoryVersion = Resources::GetVersion(openDirectory);

							auto collectItem = [&](RID asset, RID directoryRID, bool isDirectory)
							{
								ResourceObject assetObject = Resources::Read(asset);
								StringView     extension = assetObject.GetString(ResourceAsset::Extension);
								if (hiddenExtensions.Has(extension)) return;

								labelCache.Clear();
								bool renaming = renamingItem == asset;
								if (!renaming && ResourceAssets::IsUpdated(asset)) labelCache = "*";
								labelCache += assetObject.GetString(ResourceAsset::Name);
								if (!renaming) labelCache += extension;
								if (!filter.PassFilter(labelCache.CStr())) return;

								visibleItems.EmplaceBack(ContentEntry{asset, directoryRID, isDirectory});
							};

							for (RID directory : openDirectoryObject.GetSubObjectList(ResourceAssetDirectory::Directories))
							{
								ResourceObject directoryObject = Resources::Read(directory);
								RID            asset = directoryObject.GetSubObject(ResourceAssetDirectory::DirectoryAsset);
								collectItem(asset, directory, true);
							}

							for (RID asset : openDirectoryObject.GetSubObjectList(ResourceAssetDirectory::Assets))
							{
								collectItem(asset, {}, false);
							}
						}

						i32 columns = ImGui::TableGetColumnCount();
						i32 totalRows = ((i32)visibleItems.Size() + columns - 1) / columns;

						ImGuiListClipper clipper;
						clipper.Begin(totalRows);
						while (clipper.Step())
						{
							for (i32 row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
							{
								ImGui::TableNextRow();
								for (i32 col = 0; col < columns; col++)
								{
									i32 idx = row * columns + col;
									if (idx >= (i32)visibleItems.Size()) break;

									ContentEntry& entry = visibleItems[idx];
									RID           asset = entry.asset;
									bool          isDirectory = entry.isDirectory;

									ResourceObject assetObject = Resources::Read(asset);
									StringView     extension = assetObject.GetString(ResourceAsset::Extension);

									labelCache.Clear();
									bool renaming = renamingItem == asset && !ShouldRenameInTree();
									if (!renaming && ResourceAssets::IsUpdated(asset)) labelCache = "*";
									labelCache += assetObject.GetString(ResourceAsset::Name);
									if (!renaming) labelCache += extension;

									ImGuiContentItemDesc desc;
									desc.id = asset.id;
									desc.label = labelCache.CStr();
									desc.texture = isDirectory ? directoryTexture : ResourceAssets::GetThumbnail(asset);
									desc.icon = ResourceAssets::GetIcon(asset);
									desc.thumbnailScale = contentBrowserZoom;
									desc.renameItem = renaming;
									desc.selected = windowObject.HasOnReferenceArray(ProjectBrowserWindowData::SelectedItems, asset);

									ImGuiContentItemState state = ImGuiContentItem(desc);

									if (state.clicked)
									{
										if (!desc.selected)
										{
											UndoRedoScope* scope = Editor::CreateUndoRedoScope("Asset Selection");

											if (!(ImGui::IsKeyDown((ImGuiKey_LeftCtrl)) || ImGui::IsKeyDown((ImGuiKey_RightCtrl))))
											{
												ClearSelection(scope);
											}

											SelectItem(asset, scope);
										}
										newSelection = true;
										selectionInTree = false;
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

									if (state.enter)
									{
										if (isDirectory)
										{
											newOpenDirectory = entry.directoryRID;
											ClearSelection(nullptr);
										}
										else
										{
											ResourceAssets::OpenAsset(asset);
										}
									}
								}
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
						Span<RID>      selected = workspace->GetSceneEditor()->GetSelectedEntities();
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

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed((ImGuiKey_Backspace)))
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
		Editor::GetActiveWorkspace()->OpenWindow(TypeInfo<ProjectBrowserWindow>::ID());
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
		UndoRedoScope*        scope = Editor::CreateUndoRedoScope("Folder Creation");
		RID                   rid = ResourceAssets::CreateDirectory(projectBrowserWindow->GetOpenDirectory(), "New Folder", scope);
		projectBrowserWindow->SetRenameItem(rid, scope);

	}

	void ProjectBrowserWindow::AssetNew(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		UndoRedoScope*        scope = Editor::CreateUndoRedoScope("Asset Creation");
		RID                   newAsset = ResourceAssets::CreateAsset(projectBrowserWindow->GetOpenDirectory(), {eventData.userData}, "", scope);

		RID newAssetParent = Resources::GetParent(newAsset);
		projectBrowserWindow->SetRenameItem(newAssetParent, scope);

	}

	void ProjectBrowserWindow::HideExtension(StringView extension)
	{
		if (!extension.Empty())
		{
			hiddenExtensions.Emplace(extension);
		}
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
			if (StringView absolutePath = ResourceAssets::GetAbsolutePath(ResourceAssets::GetAssetPayload(openDirectory)); !absolutePath.Empty())
			{
				Platform::OpenURL(absolutePath);
			}
		}
	}

	void ProjectBrowserWindow::AssetCopyPathIdToClipboard(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* projectBrowserWindow = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		if (RID lastSelected = projectBrowserWindow->GetLastSelectedItem())
		{
			Platform::SetClipboardText(ResourceAssets::GetPathId(lastSelected));
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
				return handler->CanExtractAsset(ResourceAssets::GetAssetPayload(lastSelected));
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
				RID asset = ResourceAssets::GetAssetPayload(lastSelected);
				if (handler->CanExtractAsset(asset))
				{
					UndoRedoScope* scope = Editor::CreateUndoRedoScope("Extract Asset");
					handler->ExtractAsset(projectBrowserWindow->GetOpenDirectory(), asset, scope);
			
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
				RID asset = ResourceAssets::GetAssetPayload(lastSelected);
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
				RID asset = ResourceAssets::GetAssetPayload(lastSelected);
				RID newAsset = ResourceAssets::CreateInheritedAsset(projectBrowserWindow->GetOpenDirectory(), asset, "", scope);
				projectBrowserWindow->ClearSelection(scope);
				projectBrowserWindow->SetRenameItem(Resources::GetParent(newAsset), scope);
		
			}
		}
	}

	void ProjectBrowserWindow::ShowResourceInspector(const MenuItemEventData& eventData)
	{
		ProjectBrowserWindow* window = static_cast<ProjectBrowserWindow*>(eventData.drawData);
		if (RID selected = window->GetLastSelectedItem())
		{
			ResourceDebuggerWindow::InspectResource(selected);
		}
	}

	void ProjectBrowserWindow::RegisterType(NativeReflectType<ProjectBrowserWindow>& type)
	{
		Event::Bind<OnDropFileCallback, &ProjectBrowserWindow::OnDropFile>();

		Editor::AddMenuItem(MenuItemCreation{.itemName = "Window/Project Browser", .action = OpenProjectBrowser});

		AddMenuItem(MenuItemCreation{.itemName = "Create Inherited Asset", .icon = ICON_FA_ENVELOPE, .priority = -100, .action = CreateInherited, .visible = CanCreateInherited});

		AddMenuItem(MenuItemCreation{.itemName = "Create", .icon = ICON_FA_SQUARE_PLUS, .priority = 0});
		AddMenuItem(MenuItemCreation{.itemName = "Create/New Folder", .icon = ICON_FA_FOLDER, .priority = 0, .action = AssetNewFolder, .visible = CanCreateAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Create/New Material", .icon = ICON_FA_PAINTBRUSH, .priority = 30, .action = AssetNew, .visible = CanCreateAsset, .userData = TypeInfo<MaterialResource>::ID()});

		AddMenuItem(MenuItemCreation{.itemName = "Delete", .icon = ICON_FA_TRASH, .priority = 25, .itemShortcut{.presKey = Key::Delete}, .action = AssetDelete, .enable = CheckSelectedAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Rename", .icon = ICON_FA_PEN_TO_SQUARE, .priority = 30, .itemShortcut{.presKey = Key::F2}, .action = AssetRename, .enable = CheckSelectedAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Show in Explorer", .icon = ICON_FA_FOLDER, .priority = 240, .action = AssetShowInExplorer});
		AddMenuItem(MenuItemCreation{.itemName = "Copy Path Id", .icon = ICON_FA_COPY, .priority = 250, .action = AssetCopyPathIdToClipboard});
		AddMenuItem(MenuItemCreation{.itemName = "Show Resource Inspector", .icon = ICON_FA_MAGNIFYING_GLASS, .priority = 500, .action = ShowResourceInspector, .enable = CheckSelectedAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Reimport Asset", .icon = ICON_FA_UPLOAD, .priority = 1000, .action = ReimportAsset, .enable = CanReimportAsset});
		AddMenuItem(MenuItemCreation{.itemName = "Extract Assets", .icon = ICON_FA_EXPAND, .priority = 1010, .action = ExtractAsset, .enable = CanExtractAsset});


		type.Attribute<EditorWindowProperties>(EditorWindowProperties{
			.dockPosition = DockPosition::BottomLeft,
			.workspaceTypes = {WorkspaceTypes::All}
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
	}

	void ProjectBrowserWindowInit()
	{
		Event::Bind<OnShutdown, &ProjectBrowserWindowShutdown>();

		directoryTexture = StaticContent::GetTexture("Content/Images/FolderIcon.png");
	}
}
