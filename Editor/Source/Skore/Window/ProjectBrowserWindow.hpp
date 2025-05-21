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

#pragma once
#include "Skore/EditorCommon.hpp"
#include "Skore/MenuItem.hpp"
#include "Skore/Core/HashSet.hpp"

namespace Skore
{
	class AssetFile;

	class ProjectBrowserWindow : public EditorWindow
	{
	public:
		SK_CLASS(ProjectBrowserWindow, EditorWindow);

		ProjectBrowserWindow();
		~ProjectBrowserWindow() override;

		void Init(u32 id, VoidPtr userData) override;
		void Draw(u32 id, bool& open) override;

		AssetFile* GetLastSelectedItem() const;
		AssetFile* GetOpenDirectory() const;

		static void OpenProjectBrowser(const MenuItemEventData& eventData);
		static void AddMenuItem(const MenuItemCreation& menuItem);
		static void RegisterType(NativeReflectType<ProjectBrowserWindow>& type);

	private:
		static MenuItemContext menuItemContext;

		String                searchString;
		f32                   contentBrowserZoom = 1.0; //TODO - save in some local setting
		AssetFile*            openDirectory = nullptr;
		String                stringCache;
		HashSet<AssetFile*>   selectedItems;
		AssetFile*            lastSelectedItem = nullptr;
		AssetFile*            renamingItem = nullptr;
		HashMap<String, bool> openTreeFolders{};
		bool                  newSelection = false;
		HashSet<AssetFile*>   markedToDelete;

		void DrawPathItems();
		void DrawTreeNode(AssetFile* assetFile);
		void SetOpenDirectory(AssetFile* directory);
		void OnDropFile(StringView filePath) const;

		static bool CheckSelectedAsset(const MenuItemEventData& eventData);
		static void AssetRename(const MenuItemEventData& eventData);
		static void Shutdown();
		static void AssetNewFolder(const MenuItemEventData& eventData);
		static void AssetNew(const MenuItemEventData& eventData);
		static void AssetDelete(const MenuItemEventData& eventData);
		static void AssetShowInExplorer(const MenuItemEventData& eventData);
		static void AssetCopyPathToClipboard(const MenuItemEventData& eventData);
		static bool CanCreateAsset(const MenuItemEventData& eventData);
		static bool CanReimportAsset(const MenuItemEventData& eventData);
		static void ReimportAsset(const MenuItemEventData& eventData);
		static bool CanExtractAsset(const MenuItemEventData& eventData);
		static void ExtractAsset(const MenuItemEventData& eventData);

	};
}
