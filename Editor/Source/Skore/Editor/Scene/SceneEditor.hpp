#pragma once
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Editor/EditorTypes.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
    class EditorTransaction;
    class TransformComponent;
    struct AssetFile;
    class Component;

    enum class SimulationStatus
    {
        None       = 0,
        Simulating = 1,
        Paused     = 2
    };

    class SK_API SceneEditor
    {
    public:
        ~SceneEditor();

        Scene* GetActiveScene() const;

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
        void       MoveEntities(GameObject* parent, usize index, Span<GameObject*> objects);
        void       ChangeParent(GameObject* parent, Span<GameObject*> objects);


        Array<UUID>           GetSelectObjectUUIDS() const;

        bool IsSimulating() const;
        void StartSimulation();
        void StopSimulation();

        void DoUpdate();

        static String GetUniqueObjectName(GameObject& object, GameObject* parent = nullptr);

        HashSet<UUID>                         selectedObjects{};
        EventHandler<OnGameObjectSelection>   onGameObjectSelectionHandler{};
        EventHandler<OnGameObjectDeselection> onGameObjectDeselectionHandler{};

    private:
        AssetFile* assetFile = nullptr;
        Scene*     editorScene = nullptr;
        Scene*     simulationScene = nullptr;

        bool shouldStartSimulation{};
        bool shouldStopSimulation{};
    };
}
