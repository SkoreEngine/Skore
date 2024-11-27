#pragma once
#include "Skore/Core/HashSet.hpp"
#include "Skore/Editor/Editor.hpp"
#include "Skore/Editor/EditorTypes.hpp"
#include "Skore/Editor/MenuItem.hpp"
#include "Skore/Editor/Asset/AssetEditor.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"


namespace Skore
{
    struct AssetFile;

    class SK_API ProjectBrowserWindow : public EditorWindow
    {
    public:
        SK_BASE_TYPES(EditorWindow);

        void Init(u32 id, VoidPtr userData) override;
        void Draw(u32 id, bool& open) override;

        static void OpenProjectBrowser(const MenuItemEventData& eventData);
        static void AddMenuItem(const MenuItemCreation& menuItem);
        static void RegisterType(NativeTypeHandler<ProjectBrowserWindow>& type);

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

        void DrawPathItems();
        void DrawTreeNode(AssetFile* assetFile);
        void SetOpenDirectory(AssetFile* directory);

        static bool CheckSelectedAsset(const MenuItemEventData& eventData);
        static void AssetRename(const MenuItemEventData& eventData);
        static void Shutdown();
        static void AssetNewFolder(const MenuItemEventData& eventData);
        static void AssetNew(const MenuItemEventData& eventData);
        static void AssetDelete(const MenuItemEventData& eventData);
        static void AssetShowInExplorer(const MenuItemEventData& eventData);
        static void AssetCopyPathToClipboard(const MenuItemEventData& eventData);
        static bool CanCreateAsset(const MenuItemEventData& eventData);
    };
}
