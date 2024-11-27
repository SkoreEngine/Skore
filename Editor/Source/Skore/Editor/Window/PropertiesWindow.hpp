#pragma once

#include "Skore/Editor/EditorTypes.hpp"
#include "Skore/Editor/MenuItem.hpp"
#include "Skore/Editor/Scene/SceneEditor.hpp"

namespace Skore
{
    class PropertiesWindow : public EditorWindow
    {
    public:
        SK_BASE_TYPES(EditorWindow);

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
