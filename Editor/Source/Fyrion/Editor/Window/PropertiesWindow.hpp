#pragma once

#include "Fyrion/Editor/EditorTypes.hpp"
#include "Fyrion/Editor/MenuItem.hpp"
#include "Fyrion/Editor/Scene/SceneEditor.hpp"

namespace Fyrion
{
    class PropertiesWindow : public EditorWindow
    {
    public:
        FY_BASE_TYPES(EditorWindow);

        PropertiesWindow();
        ~PropertiesWindow() override;
        void Draw(u32 id, bool& open) override;

        static void RegisterType(NativeTypeHandler<PropertiesWindow>& type);

    private:
        SceneEditor& sceneEditor;
        String       stringCache{};
        UUID         selectedObject{};
        bool         renamingFocus{};
        String       renamingCache{};
        GameObject*  renamingObject{};
        String       searchComponentString{};
        UUID         selectedComponent = {};

        AssetFile* selectedAsset = {};

        void ClearSelection();

        static void OpenProperties(const MenuItemEventData& eventData);

        void DrawSceneObject(u32 id, GameObject* gameObject);
        void DrawAsset(u32 id, AssetFile* assetFile);

        void GameObjectSelection(UUID objectId);
        void GameObjectDeselection(UUID objectId);
        void AssetSelection(AssetFile* assetFile);
    };
}
