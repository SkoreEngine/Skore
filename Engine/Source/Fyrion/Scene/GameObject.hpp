#pragma once

#include "SceneTypes.hpp"
#include "Fyrion/Common.hpp"
#include "Fyrion/Core/Array.hpp"
#include "Fyrion/Core/HashMap.hpp"
#include "Fyrion/Core/HashSet.hpp"
#include "Fyrion/Core/Span.hpp"
#include "Fyrion/Core/String.hpp"
#include "Fyrion/Core/StringView.hpp"
#include "Fyrion/Core/UUID.hpp"

namespace Fyrion
{
    class Scene;
    class Component;

    class FY_API GameObject
    {
    public:
        friend class Scene;

        ~GameObject();

        Scene*      GetScene() const;
        GameObject* GetParent() const;
        StringView  GetName() const;
        void        SetName(StringView newName);
        UUID        GetUUID() const;

        GameObject*       Create();
        GameObject*       Create(UUID uuid);
        GameObject*       Duplicate() const;
        GameObject*       Duplicate(GameObject* parent) const;
        Span<GameObject*> GetChildren() const;
        void              RemoveChild(GameObject* gameObject);
        GameObject*       FindChildByName(StringView name) const;
        GameObject*       FindChildByUUID(UUID uuid) const;
        usize             GetIndex() const;
        void              MoveTo(usize index);
        void              SetParent(GameObject* newParent);
        bool              IsParentOf(GameObject* object) const;

        //prefabs
        void        SetPrefab(UUID prefabId);
        void        SetPrefab(GameObject* gameObject);
        GameObject* GetPrefab() const;
        GameObject* FindChildByPrefab(UUID uuid) const;
        void        RemovePrefabObject(GameObject* gameObject);
        void        RemovePrefabComponent(Component* component);

        Component*       GetComponent(TypeID typeId) const;
        Component*       GetOrAddComponent(TypeID typeId);
        Component*       FindComponentByUUID(UUID uuid) const;
        Component*       FindComponentByInstance(UUID uuid) const;
        void             GetComponentsOfType(TypeID typeId, Array<Component*>& arrComponents) const;
        Component*       AddComponent(TypeID typeId);
        Component*       AddComponent(TypeHandler* typeHandler, UUID uuid);
        void             RemoveComponent(Component* component);
        Span<Component*> GetComponents() const;

        //prefab components
        void AddComponentOverride(Component* component);
        void RemoveComponentOverride(Component* component, bool resetValue = true);
        bool IsComponentOverride(Component* component);

        ArchiveValue Serialize(ArchiveWriter& writer) const;
        void         Deserialize(ArchiveReader& reader, ArchiveValue value);

        void NotifyEvent(const SceneEventDesc& event);
        void Destroy();

        template <typename T>
        T* GetComponent()
        {
            return static_cast<T*>(GetComponent(GetTypeID<T>()));
        }

        template <typename T>
        T* GetOrAddComponent()
        {
            return static_cast<T*>(GetOrAddComponent(GetTypeID<T>()));
        }

        template <typename T>
        T* AddComponent()
        {
            return static_cast<T*>(AddComponent(GetTypeID<T>()));
        }

        friend class Scene;

    private:
        GameObject(Scene* scene);
        GameObject(Scene* scene, GameObject* parent);

        struct PrefabInstance
        {
            GameObject*   object = nullptr;
            HashSet<UUID> modifiedComponents;
            HashSet<UUID> removedComponents;
            HashSet<UUID> removedObjects;
        };

        Scene*      scene;
        GameObject* parent;
        String      name;
        UUID        uuid;
        bool        started = false;

        //prefabs
        PrefabInstance prefab;

        Array<GameObject*> children;
        Array<Component*>  components;

        void        Start();
        GameObject* CreateInternal(UUID uuid, GameObject* parent) const;
        static void CopyComponents(GameObject* dest, GameObject* origin);

        GameObject* GetPrefabObject(UUID uuid) const;
    };
}
