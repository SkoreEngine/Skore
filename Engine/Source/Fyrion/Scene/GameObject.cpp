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

        for (Component* component : components)
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
        if (parent == nullptr)
        {
            return scene->GetName();
        }
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
        return instance.object;
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
        for (Component* component : origin->components)
        {
            Component* newComponent = dest->AddComponent(component->typeHandler, UUID::RandomUUID());
            newComponent->typeHandler->DeepCopy(component, newComponent);
        }
    }

    GameObject* GameObject::FindByInstance(UUID uuid) const
    {
        for (GameObject* child : children)
        {
            if (child->instance.id == uuid)
            {
                return child;
            }
        }
        return nullptr;
    }

    void GameObject::InitPrefab(GameObject* objectPrefab)
    {
        instance.object = objectPrefab;

        if (name.Empty())
        {
            SetName(objectPrefab->GetName());
        }

        for (Component* component : objectPrefab->components)
        {
            //check if GameObject already have this component.
            Component* newComponent = AddComponent(component->typeHandler, UUID::RandomUUID());
            newComponent->typeHandler->DeepCopy(component, newComponent);
            newComponent->prefab = component;
        }

        for (GameObject* child : objectPrefab->children)
        {
            GameObject* childPrefab = FindByInstance(child->uuid);
            if (!childPrefab)
            {
                childPrefab = this->CreateInternal(UUID::RandomUUID());
            }
            childPrefab->instance.id = child->uuid;
            childPrefab->InitPrefab(child);
            childPrefab->Start();
        }
    }

    GameObject* GameObject::Create()
    {
        return CreateInternal(UUID::RandomUUID());
    }

    GameObject* GameObject::Create(Scene* scene)
    {
        GameObject* gameObject = CreateInternal(UUID::RandomUUID());

        if (scene)
        {
            gameObject->instance.scene = scene;
            gameObject->InitPrefab(&scene->GetRootObject());
        }

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
        component->typeHandler = typeHandler;
        component->gameObject = this;
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
            RemoveComponentOverride(component, false);
            component->OnDestroy();
            component->typeHandler->Destroy(component);
            components.Erase(it);
        }
    }

    Span<Component*> GameObject::GetComponents() const
    {
        return components;
    }

    void GameObject::AddComponentOverride(Component* component)
    {
        // if (prefab != nullptr)
        // {
        //     overrideComponents.Insert(component->uuid, component);
        // }
    }

    void GameObject::RemoveComponentOverride(Component* component, bool resetValue)
    {
        // if (prefab != nullptr)
        // {
        //     overrideComponents.Erase(component->uuid);
        //     if (resetValue)
        //     {
        //         if (Component* originalValue = prefab->GetComponentByUUID(component->uuid))
        //         {
        //             component->typeHandler->DeepCopy(originalValue, component);
        //             component->OnChange();
        //         }
        //     }
        // }
    }

    bool GameObject::IsComponentOverride(Component* component)
    {
        //return overrideComponents.Has(component->uuid);
        return false;
    }

    ArchiveValue GameObject::Serialize(ArchiveWriter& writer) const
    {
        ArchiveValue object = writer.CreateObject();

        if (!name.Empty() && (!instance.object || instance.object->name != name))
        {
            writer.AddToObject(object, "name", writer.StringValue(name));
        }

        writer.AddToObject(object, "uuid", writer.StringValue(uuid.ToString()));

        if (instance.id)
        {
            writer.AddToObject(object, "instance", writer.StringValue(instance.id.ToString()));
        }

        if (instance.scene)
        {
            writer.AddToObject(object, "scene", writer.StringValue(instance.scene->GetUUID().ToString()));
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
            if (component->prefab != nullptr) continue;

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
        usize        arrChildrenSize = reader.ArraySize(arrChildren);

        ArchiveValue vlChild{};
        for (usize c = 0; c < arrChildrenSize; ++c)
        {
            vlChild = reader.ArrayNext(arrChildren, vlChild);
            UUID childUUID   = UUID::FromString(reader.StringValue(reader.GetObjectValue(vlChild, "uuid")));
            GameObject* child = CreateInternal(childUUID);

            child->Deserialize(reader, vlChild);
        }

        ArchiveValue arrComponent = reader.GetObjectValue(value, "components");
        usize        arrComponentSize = reader.ArraySize(arrComponent);

        ArchiveValue vlComponent{};
        for (usize c = 0; c < arrComponentSize; ++c)
        {
            vlComponent = reader.ArrayNext(arrComponent, vlComponent);

            if (StringView typeName = reader.StringValue(reader.GetObjectValue(vlComponent, "_type")); !typeName.Empty())
            {
                if (TypeHandler* typeHandler = Registry::FindTypeByName(typeName))
                {
                    UUID uuid = UUID::FromString(reader.StringValue(reader.GetObjectValue(vlComponent, "_uuid")));
                    Component* component = AddComponent(typeHandler, uuid);
                    Serialization::Deserialize(typeHandler, reader, vlComponent, component);
                }
            }
        }

        instance.id = UUID::FromString(reader.StringValue(reader.GetObjectValue(value, "instance")));

        if (UUID sceneId = UUID::FromString(reader.StringValue(reader.GetObjectValue(value, "scene"))))
        {
            instance.scene = Assets::Load<Scene>(sceneId);
            if (instance.scene)
            {
                InitPrefab(&instance.scene->GetRootObject());
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
