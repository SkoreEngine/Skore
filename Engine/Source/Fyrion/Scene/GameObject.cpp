#include "GameObject.hpp"
#include "Scene.hpp"
#include "Component/Component.hpp"
#include "Fyrion/Core/Registry.hpp"

namespace Fyrion
{
    GameObject::GameObject(Scene* scene) : scene(scene), parent(nullptr) {}
    GameObject::GameObject(Scene* scene, GameObject* parent) : scene(scene), parent(parent) {}


    GameObject::~GameObject()
    {
        if (parent)
        {
            parent->RemoveChild(this);
        }

        if (scene && uuid)
        {
            scene->objectsById.Erase(uuid);
        }

        for(Component* component : components)
        {
            component->OnDestroy();
            component->typeHandler->Destroy(component);
        }

        for (GameObject* child : children)
        {
            child->parent = nullptr;
            MemoryGlobals::GetDefaultAllocator().DestroyAndFree(child);
        }
    }

    Scene* GameObject::GetScene() const
    {
        return scene;
    }

    GameObject* GameObject::GetParent() const
    {
        return parent;
    }

    StringView GameObject::GetName() const
    {
        return name;
    }

    void GameObject::SetName(StringView newName)
    {
        name = newName;
    }

    UUID GameObject::GetUUID() const
    {
        if (parent == nullptr)
        {
            return scene->GetUUID();
        }
        return uuid;
    }

    GameObject* GameObject::GetPrefab() const
    {
        return prefab;
    }

    Span<GameObject*> GameObject::GetChildren() const
    {
        return children;
    }

    GameObject* GameObject::CreateInternal(UUID uuid)
    {
        GameObject* gameObject = new(MemoryGlobals::GetDefaultAllocator().MemAlloc(sizeof(GameObject), alignof(GameObject))) GameObject{scene, this};
        gameObject->uuid = uuid;
        children.EmplaceBack(gameObject);

        scene->objectsById.Insert(uuid, gameObject);
        scene->queueToStart.EmplaceBack(gameObject);

        return gameObject;
    }

    void GameObject::CopyComponents(GameObject* dest, GameObject* origin)
    {
        for(Component* component : origin->components)
        {
            if (dest->GetComponentByUUID(component->uuid) == nullptr)
            {
                Component* newComponent = dest->AddComponent(component->typeHandler, component->uuid);
                newComponent->typeHandler->DeepCopy(component, newComponent);
                newComponent->gameObject = dest;
            }
            else
            {
                dest->prefabOverride.Emplace(component->uuid);
            }
        }
    }

    void GameObject::InitPrefab(GameObject* object)
    {
        if (object->prefab)
        {
            CopyComponents(object, object->prefab);

            for (GameObject* child : object->prefab->children)
            {
                GameObject* childPrefab = object->CreateInternal(UUID::RandomUUID());
                childPrefab->prefab = child;
                childPrefab->SetName(child->GetName());

                CopyComponents(childPrefab, child);

                childPrefab->Start();
            }
        }
    }

    GameObject* GameObject::Create()
    {
        return CreateInternal(UUID::RandomUUID());
    }

    GameObject* GameObject::Create(GameObject* prefab)
    {
        GameObject* gameObject =  CreateInternal(UUID::RandomUUID());
        gameObject->prefab = prefab;
        InitPrefab(gameObject);
        return gameObject;
    }

    void GameObject::RemoveChild(GameObject* gameObject)
    {
        if (auto it = FindFirst(children.begin(), children.end(), gameObject))
        {
            children.Erase(it);
        }
    }

    GameObject* GameObject::FindChildByName(StringView name) const
    {
        for (GameObject* child : children)
        {
            if (child->name == name)
            {
                return child;
            }
        }

        return nullptr;
    }

    Component* GameObject::GetComponent(TypeID typeId) const
    {
        for (Component* component : components)
        {
            if (component->typeHandler->GetTypeInfo().typeId == typeId)
            {
                return component;
            }
        }
        return nullptr;
    }

    Component* GameObject::GetComponentByUUID(UUID uuid) const
    {
        for (Component* component : components)
        {
            if (component->uuid == uuid)
            {
                return component;
            }
        }
        return nullptr;
    }

    Component* GameObject::GetOrAddComponent(TypeID typeId)
    {
        Component* component = GetComponent(typeId);
        if (component == nullptr)
        {
            component = AddComponent(typeId);
        }
        return component;
    }

    void GameObject::GetComponentsOfType(TypeID typeId, Array<Component*> arrComponents) const
    {
        arrComponents.Clear();
        for (Component* component : components)
        {
            if (component->typeHandler->GetTypeInfo().typeId == typeId)
            {
                arrComponents.EmplaceBack(component);
            }
        }
    }

    Component* GameObject::AddComponent(TypeID typeId)
    {
        if (TypeHandler* typeHandler = Registry::FindTypeById(typeId))
        {
            return AddComponent(typeHandler, UUID::RandomUUID());
        }
        return nullptr;
    }

    Component* GameObject::AddComponent(TypeHandler* typeHandler, UUID uuid)
    {
        Component* component = static_cast<Component*>(typeHandler->NewObject());
        component->gameObject = this;
        component->typeHandler = typeHandler;
        component->uuid = uuid;
        components.EmplaceBack(component);

        if (started)
        {
            scene->componentsToStart.EmplaceBack(component);
        }

        return component;
    }

    void GameObject::RemoveComponent(Component* component)
    {
        if (auto it = FindFirst(components.begin(), components.end(), component))
        {
            component->OnDestroy();
            component->typeHandler->Destroy(component);
            components.Erase(it);
        }
    }

    Span<Component*> GameObject::GetComponents() const
    {
        return components;
    }

    void GameObject::AddPrefabOverride(Component* component)
    {
        prefabOverride.Insert(component->uuid);
    }

    ArchiveValue GameObject::Serialize(ArchiveWriter& writer) const
    {
        if (!GetUUID())
        {
            return {};
        }

        ArchiveValue object = writer.CreateObject();

        if (!name.Empty() && (prefab == nullptr || name != prefab->GetName()))
        {
            writer.AddToObject(object, "name", writer.StringValue(name));
        }

        if (uuid)
        {
            writer.AddToObject(object, "uuid", writer.StringValue(uuid.ToString()));
        }

        if (prefab)
        {
            writer.AddToObject(object, "prefab", writer.StringValue(prefab->GetUUID().ToString()));
        }

        ArchiveValue childrenArr{};

        for (GameObject* child : children)
        {
            if (ArchiveValue valueChild = child->Serialize(writer))
            {
                if (!childrenArr)
                {
                    childrenArr = writer.CreateArray();
                }
                writer.AddToArray(childrenArr, valueChild);
            }
        }

        if (childrenArr)
        {
            writer.AddToObject(object, "children", childrenArr);
        }

        ArchiveValue componentArr{};

        for (const Component* component : components)
        {
            if (prefab != nullptr && !prefabOverride.Has(component->uuid))
            {
                continue;
            }

            ArchiveValue componentValue = Serialization::Serialize(component->typeHandler, writer, component);
            writer.AddToObject(componentValue, "_type", writer.StringValue(component->typeHandler->GetName()));
            writer.AddToObject(componentValue, "_uuid", writer.StringValue(component->uuid.ToString()));
            if (!componentArr)
            {
                componentArr = writer.CreateArray();
            }
            writer.AddToArray(componentArr, componentValue);
        }

        if (componentArr)
        {
            writer.AddToObject(object, "components", componentArr);
        }

        return object;
    }

    void GameObject::Deserialize(ArchiveReader& reader, ArchiveValue value)
    {
        if (StringView name = reader.StringValue(reader.GetObjectValue(value, "name")); !name.Empty())
        {
            SetName(name);
        }

        ArchiveValue arrChildren = reader.GetObjectValue(value, "children");
        usize arrChildrenSize = reader.ArraySize(arrChildren);

        ArchiveValue vlChildren{};
        for (usize c = 0; c < arrChildrenSize; ++c)
        {
            vlChildren = reader.ArrayNext(arrChildren, vlChildren);
            UUID uuid   = UUID::FromString(reader.StringValue(reader.GetObjectValue(vlChildren, "uuid")));

            GameObject* child = CreateInternal(uuid);
            if (UUID prefab = UUID::FromString(reader.StringValue(reader.GetObjectValue(vlChildren, "prefab"))))
            {
                child->prefab = &Assets::Load<Scene>(prefab)->GetRootObject();
            }
            child->Deserialize(reader, vlChildren);
            InitPrefab(child);
        }

        ArchiveValue arrComponent = reader.GetObjectValue(value, "components");
        usize arrComponentSize = reader.ArraySize(arrComponent);

        ArchiveValue vlComponent{};
        for (usize c = 0; c < arrComponentSize; ++c)
        {
            vlComponent = reader.ArrayNext(arrComponent, vlComponent);

            if (StringView typeName = reader.StringValue(reader.GetObjectValue(vlComponent, "_type")); !typeName.Empty())
            {
                TypeHandler* typeHandler = Registry::FindTypeByName(typeName);
                if (typeHandler)
                {
                    UUID uuid = UUID::FromString(reader.StringValue(reader.GetObjectValue(vlComponent, "_uuid")));
                    Component* component = AddComponent(typeHandler, uuid);
                    Serialization::Deserialize(typeHandler, reader, vlComponent, component);
                }
            }
        }
    }

    void GameObject::Start()
    {
        started = true;

        for (Component* component : components)
        {
            component->OnStart();
        }

        for (GameObject* child : children)
        {
            child->Start();
        }
    }

    void GameObject::NotifyEvent(const SceneEventDesc& event)
    {
        for (Component* component : components)
        {
            component->ProcessEvent(event);
        }
    }

    void GameObject::Destroy()
    {
        scene->DestroyGameObject(this);
    }
}
