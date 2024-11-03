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

        GameObject* GetPrefab() const;

        GameObject*       Create();
        GameObject*       Create(Scene* prefab);
        Span<GameObject*> GetChildren() const;
        void              RemoveChild(GameObject* gameObject);
        GameObject*       FindChildByName(StringView name) const;

        Component*       GetComponent(TypeID typeId) const;
        Component*       GetComponentByUUID(UUID uuid) const;
        Component*       GetOrAddComponent(TypeID typeId);
        void             GetComponentsOfType(TypeID typeId, Array<Component*> arrComponents) const;
        Component*       AddComponent(TypeID typeId);
        Component*       AddComponent(TypeHandler* typeHandler, UUID uuid);
        void             RemoveComponent(Component* component);
        Span<Component*> GetComponents() const;
        void             AddComponentOverride(Component* component);
        void             RemoveComponentOverride(Component* component, bool resetValue = true);
        bool             IsComponentOverride(Component* component);

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

        Scene*      scene;
        GameObject* parent;
        GameObject* prefab = nullptr;
        String      name;
        UUID        uuid;
        bool        started = false;

        Array<GameObject*>        children;
        Array<Component*>         components;
        HashMap<UUID, Component*> overrideComponents;

        void        Start();
        GameObject* CreateInternal(UUID uuid);
        static void CopyComponents(GameObject* dest, GameObject* origin);

        void       InitPrefab(GameObject* objectPrefab);
        void       AddComponentInternal(Component* component);

        GameObject* GetPrefabInstance();
    };
}
