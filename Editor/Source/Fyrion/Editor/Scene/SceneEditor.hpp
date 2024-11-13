#pragma once
#include "Fyrion/Core/HashSet.hpp"
#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Editor/EditorTypes.hpp"
#include "Fyrion/Scene/Scene.hpp"

namespace Fyrion
{
    class EditorTransaction;
    class TransformComponent;
    struct AssetFile;
    class Component;

    class FY_API SceneEditor
    {
    public:
        Scene*     GetScene() const;
        AssetFile* GetAssetFile() const;
        void       SetScene(AssetFile* assetFile);
        void       ClearSelection(EditorTransaction* transaction);
        void       SelectObject(GameObject& object, EditorTransaction* transaction);
        void       DeselectObject(GameObject& object, EditorTransaction* transaction);
        void       ClearSelectionNoHistory();
        void       SelectObjectNoHistory(GameObject& object);
        void       SelectObjectsNoHistory(Span<UUID> ids);
        void       DeselectObjectNoHistory(GameObject& object);
        bool       IsSelected(GameObject& object) const;
        bool       IsParentOfSelected(GameObject& object) const;
        void       RenameObject(GameObject& object, StringView newName);
        void       DestroySelectedObjects();
        void       CreateGameObject(UUID prefab, bool checkSelected);
        void       DuplicateSelected();
        bool       IsValidSelection();
        void       AddComponent(GameObject* gameObject, TypeHandler* typeHandler);
        void       ResetComponent(GameObject* gameObject, Component* component);
        void       RemoveComponent(GameObject* gameObject, Component* component);
        void       UpdateComponent(GameObject* gameObject, Component* instance);
        void       UpdateTransform(GameObject* gameObject, const Transform& oldTransform, TransformComponent* transformComponent);
        void       RemoveComponentOverride(GameObject* gameObject, Component* component);
        void       MarkDirty();
        void       MoveEntities(GameObject* parent, usize index, Span<GameObject*> entities);
        void       ChangeParent(GameObject* parent, Span<GameObject*> entities);


        HashSet<GameObject*>& GetSelectedObjects();
        Array<UUID>           GetSelectObjectUUIDS() const;

        bool IsSimulating() const;
        void StartSimulation();
        void StopSimulation();

        void DoUpdate();

        static String GetUniqueObjectName(GameObject& object, GameObject* parent = nullptr);

        HashSet<GameObject*>                  selectedObjects{};
        EventHandler<OnGameObjectSelection>   onGameObjectSelectionHandler{};
        EventHandler<OnGameObjectDeselection> onGameObjectDeselectionHandler{};

    private:
        AssetFile* assetFile = nullptr;
        Scene*     scene = nullptr;
    };
}
