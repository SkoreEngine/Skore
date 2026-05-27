#pragma once
#include "imgui.h"
#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
#include "Skore/Resource/ResourceCommon.hpp"


namespace Skore
{
	class SK_API ProjectBrowserWindow : public EditorWindow
	{
	public:
		SK_CLASS(ProjectBrowserWindow, EditorWindow);

		~ProjectBrowserWindow() override;

		const char* GetTitle() const override;
		void        Init(VoidPtr userData) override;
		void        Draw(bool& open) override;

		static void OpenProjectBrowser(const MenuItemEventData& eventData);
		static void AddMenuItem(const MenuItemCreation& menuItem);
		static void RegisterType(NativeReflectType<ProjectBrowserWindow>& type);
		static bool CanCreateAsset(const MenuItemEventData& eventData);
		static void AssetNew(const MenuItemEventData& eventData);

		static void HideExtension(StringView extension);

		void ClearSelection(UndoRedoScope* scope);
		void SelectItem(RID rid, UndoRedoScope* scope);

		void SetRenameItem(RID rid, UndoRedoScope* scope);
		RID  GetOpenDirectory() const;
		RID  GetLastSelectedItem() const;

	private:
		static MenuItemContext menuItemContext;

		struct ContentEntry
		{
			RID  asset;
			RID  directoryRID;
			bool isDirectory;
		};

		RID cachedDirectory = {};
		u64 cachedDirectoryVersion = {};
		Array<ContentEntry> visibleItems;

		String             searchString;
		f32                contentBrowserZoom = 1.0;
		String             stringCache;
		HashMap<RID, bool> openTreeFolders{};
		RID                windowObjectRID;
		bool               newSelection = false;
		bool               treeOnlyView = false;
		bool               selectionInTree = false;
		bool               renameFocusSet = false;
		String             renameBuffer;
		RID                popupFolder = {};
		Array<RID>         directoryCache;
		ImGuiTextFilter    filter;

		void DrawPathItems();
		void DrawDirectoryTreeNode(RID asset);
		void DrawAssetTreeLeaf(RID asset);
		bool DrawTreeRenameInput(RID asset, const String& currentName);
		bool ShouldRenameInTree() const;
		void SetOpenDirectory(RID rid);

		static void OnDropFile(StringView filePath);

		static void AssetRename(const MenuItemEventData& eventData);
		static void AssetDelete(const MenuItemEventData& eventData);
		static bool CheckSelectedAsset(const MenuItemEventData& eventData);
		static void AssetNewFolder(const MenuItemEventData& eventData);
		static void AssetShowInExplorer(const MenuItemEventData& eventData);
		static void AssetCopyPathIdToClipboard(const MenuItemEventData& eventData);
		static bool CanReimportAsset(const MenuItemEventData& eventData);
		static void ReimportAsset(const MenuItemEventData& eventData);
		static bool CanExtractAsset(const MenuItemEventData& eventData);
		static void ExtractAsset(const MenuItemEventData& eventData);

		static bool CanCreateInherited(const MenuItemEventData& eventData);
		static void CreateInherited(const MenuItemEventData& eventData);
		static void ShowResourceInspector(const MenuItemEventData& eventData);
	};
}
