#pragma once
#include "Fyrion/Core/HashSet.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Editor/EditorTypes.hpp"
#include "Fyrion/Scene/Scene.hpp"

namespace Fyrion
{
    class TransformComponent;
    struct AssetFile;
    class Component;

    class SceneEditor
    {
    public:
        Scene*     GetScene() const;
        AssetFile* GetAssetFile() const;
        void       SetScene(AssetFile* assetFile);
        void       ClearSelection();
        void       SelectObject(GameObject& object);
        void       DeselectObject(GameObject& object);
        bool       IsSelected(GameObject& object) const;
        bool       IsParentOfSelected(GameObject& object) const;
        void       RenameObject(GameObject& object, StringView newName);
        void       DestroySelectedObjects();
        void       CreateGameObject(Scene* prefab, bool checkSelected);
        bool       IsValidSelection();
        void       AddComponent(GameObject* gameObject, TypeHandler* typeHandler);
        void       ResetComponent(GameObject* gameObject, Component* component);
        void       RemoveComponent(GameObject* gameObject, Component* component);
        void       UpdateComponent(GameObject* gameObject, Component* instance);
        void       UpdateTransform(GameObject* gameObject, const Transform& oldTransform, TransformComponent* transformComponent);
        void       RemoveComponentOverride(GameObject* gameObject, Component* component);


        HashSet<GameObject*>& GetSelectedObjects();

        bool IsSimulating() const;
        void StartSimulation();
        void StopSimulation();

        void DoUpdate();

    private:
        AssetFile*           assetFile = nullptr;
        Scene*               scene = nullptr;
        HashSet<GameObject*> selectedObjects{};

        EventHandler<OnGameObjectSelection> onGameObjectSelectionHandler{};
    };
}
