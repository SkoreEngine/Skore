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
            RemoveComponentOverride(component, false);
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
        for (Component* component : origin->components)
        {
            Component* newComponent = dest->AddComponent(component->typeHandler, component->uuid);
            newComponent->typeHandler->DeepCopy(component, newComponent);
        }
    }

    void GameObject::InitPrefab(GameObject* objectPrefab)
    {
        prefab = objectPrefab;

        SetName(objectPrefab->GetName());

        for (Component* component : objectPrefab->components)
        {
            if (auto it = GetPrefabInstance()->overrideComponents.Find(component->uuid))
            {
                AddComponentInternal(it->second);
            }
            else
            {
                Component* newComponent = AddComponent(component->typeHandler, component->uuid);
                newComponent->typeHandler->DeepCopy(component, newComponent);
            }
        }

        for (GameObject* child : objectPrefab->children)
        {
            GameObject* childPrefab = this->CreateInternal(UUID::RandomUUID());
            childPrefab->prefab = prefab;
            childPrefab->SetName(child->GetName());
            childPrefab->InitPrefab(child);
            childPrefab->Start();
        }
    }

    GameObject* GameObject::GetPrefabInstance()
    {
        //TODO, maybe cache it in the object?
        if (parent != nullptr)
        {
            if (parent->prefab == nullptr)
            {
                return this;
            }

            return parent->GetPrefabInstance();
        }
        return nullptr;
    }

    GameObject* GameObject::Create()
    {
        return CreateInternal(UUID::RandomUUID());
    }

    GameObject* GameObject::Create(Scene* prefab)
    {
        GameObject* gameObject = CreateInternal(UUID::RandomUUID());
        if (prefab)
        {
            gameObject->InitPrefab(&prefab->GetRootObject());
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
        component->typeHandler = typeHandler;
        component->uuid = uuid;
        AddComponentInternal(component);
        return component;
    }

    void GameObject::AddComponentInternal(Component* component)
    {
        component->gameObject = this;
        components.EmplaceBack(component);
        if (started)
        {
            scene->componentsToStart.EmplaceBack(component);
        }
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
        if (prefab != nullptr)
        {
            if (GameObject* prefabInstance = GetPrefabInstance())
            {
                prefabInstance->overrideComponents.Insert(component->uuid, component);
            }
        }
    }

    void GameObject::RemoveComponentOverride(Component* component, bool resetValue)
    {
        if (prefab != nullptr)
        {
            if (GameObject* prefabInstance = GetPrefabInstance())
            {
                prefabInstance->overrideComponents.Erase(component->uuid);
                if (resetValue)
                {
                    if (Component* originalValue = prefab->GetComponentByUUID(component->uuid))
                    {
                        component->typeHandler->DeepCopy(originalValue, component);
                        component->OnChange();
                    }
                }
            }
        }
    }

    bool GameObject::IsComponentOverride(Component* component)
    {
        if (GameObject* prefabInstance = GetPrefabInstance())
        {
            return prefabInstance->overrideComponents.Has(component->uuid);
        }
        return false;

    }

    ArchiveValue GameObject::Serialize(ArchiveWriter& writer) const
    {
        ArchiveValue object = writer.CreateObject();

        if (!name.Empty() && prefab == nullptr)
        {
            writer.AddToObject(object, "name", writer.StringValue(name));
        }

        writer.AddToObject(object, "uuid", writer.StringValue(uuid.ToString()));

        if (!prefab)
        {
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
        }
        else
        {
            writer.AddToObject(object, "prefab", writer.StringValue(prefab->GetUUID().ToString()));

            if (!overrideComponents.Empty())
            {
                ArchiveValue overrideArr = writer.CreateArray();
                for (auto it : overrideComponents)
                {
                    Component*   component = it.second;
                    ArchiveValue componentValue = Serialization::Serialize(component->typeHandler, writer, component);
                    writer.AddToObject(componentValue, "_type", writer.StringValue(component->typeHandler->GetName()));
                    writer.AddToObject(componentValue, "_uuid", writer.StringValue(component->uuid.ToString()));
                    writer.AddToArray(overrideArr, componentValue);
                }
                writer.AddToObject(object, "componentOverrides", overrideArr);
            }
        }
        return object;
    }

    void GameObject::Deserialize(ArchiveReader& reader, ArchiveValue value)
    {
        auto deserializeComponent = [](ArchiveReader& reader, ArchiveValue vlComponent) -> Component*
        {
            if (StringView typeName = reader.StringValue(reader.GetObjectValue(vlComponent, "_type")); !typeName.Empty())
            {
                if (TypeHandler* typeHandler = Registry::FindTypeByName(typeName))
                {
                    Component* component = static_cast<Component*>(typeHandler->NewObject());
                    Serialization::Deserialize(typeHandler, reader, vlComponent, component);
                    component->uuid = UUID::FromString(reader.StringValue(reader.GetObjectValue(vlComponent, "_uuid")));
                    component->typeHandler = typeHandler;
                    return component;
                }
            }
            return nullptr;
        };


        if (StringView name = reader.StringValue(reader.GetObjectValue(value, "name")); !name.Empty())
        {
            SetName(name);
        }

        if (UUID prefabId = UUID::FromString(reader.StringValue(reader.GetObjectValue(value, "prefab"))))
        {
            if (Scene* prefabScene = Assets::Load<Scene>(prefabId))
            {
                ArchiveValue arrComponentOverrides = reader.GetObjectValue(value, "componentOverrides");
                usize        arrComponentOverridesSize = reader.ArraySize(arrComponentOverrides);

                ArchiveValue vlComponenOverride{};
                for (usize c = 0; c < arrComponentOverridesSize; ++c)
                {
                    vlComponenOverride = reader.ArrayNext(arrComponentOverrides, vlComponenOverride);
                    if (Component* component = deserializeComponent(reader, vlComponenOverride))
                    {
                        overrideComponents.Insert(component->uuid, component);
                    }
                }
                InitPrefab(&prefabScene->GetRootObject());
            }
        }
        else
        {
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
                AddComponentInternal(deserializeComponent(reader, vlComponent));
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
