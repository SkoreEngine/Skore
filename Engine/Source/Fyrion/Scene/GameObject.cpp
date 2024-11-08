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

    GameObject* GameObject::GetInstance() const
    {
        return instance.object;
    }

    Scene* GameObject::GetSceneInstance() const
    {
        if (instance.scene)
        {
            return instance.scene;
        }

        if (instance.object)
        {
            return instance.object->scene;
        }
        return nullptr;
    }

    Span<GameObject*> GameObject::GetChildren() const
    {
        return children;
    }

    GameObject* GameObject::CreateInternal(UUID uuid, GameObject* parent) const
    {
        GameObject* gameObject = new(MemoryGlobals::GetDefaultAllocator().MemAlloc(sizeof(GameObject), alignof(GameObject))) GameObject{parent->scene, parent};
        gameObject->uuid = uuid;
        parent->children.EmplaceBack(gameObject);

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
            if (child->instance.object && child->instance.object->uuid == uuid)
            {
                return child;
            }
        }
        return nullptr;
    }

    Component* GameObject::FindComponentByInstance(UUID uuid) const
    {
        for (Component* component : components)
        {
            if (component->instance && component->instance->uuid == uuid)
            {
                return component;
            }
        }
        return nullptr;
    }

    void GameObject::InitInstance()
    {
        if (!instance.object) return;

        if (name.Empty())
        {
            SetName(instance.object->GetName());
        }

        for (Component* component : instance.object->components)
        {
            if (instance.removedComponents.Has(component->uuid)) continue;


            if (FindComponentByInstance(component->uuid) == nullptr)
            {
                Component* newComponent = AddComponent(component->typeHandler, UUID::RandomUUID());
                newComponent->typeHandler->DeepCopy(component, newComponent);
                newComponent->instance = component;
            }
        }

        for (GameObject* child : instance.object->children)
        {
            if (instance.removedObjects.Find(child->uuid)) continue;

            GameObject* childInstance = FindByInstance(child->uuid);
            if (!childInstance)
            {
                childInstance = this->CreateInternal(UUID::RandomUUID(), this);
                childInstance->instance.object = child;
            }
            childInstance->InitInstance();
            childInstance->Start();
        }
    }

    GameObject* GameObject::Create()
    {
        return CreateInternal(UUID::RandomUUID(), this);
    }

    GameObject* GameObject::Create(Scene* instance)
    {
        return Create(UUID::RandomUUID(), instance);
    }

    GameObject* GameObject::Create(UUID uuid, Scene* instance)
    {
        GameObject* gameObject = CreateInternal(uuid, this);

        if (instance)
        {
            gameObject->instance.scene = instance;
            gameObject->instance.object = &instance->GetRootObject();
            gameObject->InitInstance();
        }

        return gameObject;
    }

    GameObject* GameObject::Duplicate() const
    {
        return Duplicate(parent);
    }

    GameObject* GameObject::Duplicate(GameObject* parent) const
    {
        GameObject* gameObject = CreateInternal(UUID::RandomUUID(), parent);
        gameObject->SetName(GetName());
        gameObject->instance = instance;

        for (Component* component : components)
        {
            if (component->instance != nullptr && !instance.modifiedComponents.Has(component->uuid)) continue;

            Component* newComponent = gameObject->AddComponent(component->typeHandler, UUID::RandomUUID());
            newComponent->typeHandler->DeepCopy(component, newComponent);
        }

        for (GameObject* child : children)
        {
            child->Duplicate(gameObject);
        }

        if (gameObject->instance.scene)
        {
            gameObject->InitInstance();
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

    Component* GameObject::FindComponentByUUID(UUID uuid) const
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
        if (instance.object)
        {
            instance.modifiedComponents.Emplace(component->uuid);
        }
    }

    void GameObject::RemoveComponentOverride(Component* component, bool resetValue)
    {
        if (instance.object)
        {
            instance.modifiedComponents.Erase(component->uuid);
            if (resetValue)
            {
                component->typeHandler->DeepCopy(component->instance, component);
                component->OnChange();
            }
        }
    }

    bool GameObject::IsComponentOverride(Component* component)
    {
        if (instance.object)
        {
            return instance.modifiedComponents.Has(component->uuid);
        }
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

        if (instance.object && instance.object->uuid)
        {
            writer.AddToObject(object, "instance", writer.StringValue(instance.object->uuid.ToString()));
        }

        if (instance.scene)
        {
            writer.AddToObject(object, "scene", writer.StringValue(instance.scene->GetUUID().ToString()));
        }

        if (!instance.removedObjects.Empty())
        {
            ArchiveValue removedArr = writer.CreateArray();
            for(auto& it: instance.removedObjects)
            {
                writer.AddToArray(removedArr, writer.StringValue(it.first.ToString()));
            }
            writer.AddToObject(object, "removedObjects", removedArr);
        }

        if (!instance.removedComponents.Empty())
        {
            ArchiveValue removedArr = writer.CreateArray();
            for(auto& it: instance.removedComponents)
            {
                writer.AddToArray(removedArr, writer.StringValue(it.first.ToString()));
            }
            writer.AddToObject(object, "removedComponents", removedArr);
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
            if (component->instance != nullptr && !instance.modifiedComponents.Has(component->uuid)) continue;

            ArchiveValue componentValue = Serialization::Serialize(component->typeHandler, writer, component);
            writer.AddToObject(componentValue, "_type", writer.StringValue(component->typeHandler->GetName()));
            writer.AddToObject(componentValue, "_uuid", writer.StringValue(component->uuid.ToString()));
            if (component->instance)
            {
                writer.AddToObject(componentValue, "_instance", writer.StringValue(component->instance->uuid.ToString()));
            }

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

        if (UUID sceneId = UUID::FromString(reader.StringValue(reader.GetObjectValue(value, "scene"))))
        {
            instance.scene = Assets::Load<Scene>(sceneId);
            if (instance.scene)
            {
                instance.object = &instance.scene->GetRootObject();
            }
        }

        if (UUID instanceId = UUID::FromString(reader.StringValue(reader.GetObjectValue(value, "instance"))))
        {
            if (Scene* sceneInstance = parent->GetSceneInstance())
            {
               instance.object = sceneInstance->FindObjectByUUID(instanceId);
            }
        }

        {
            ArchiveValue removedObjects = reader.GetObjectValue(value, "removedObjects");
            usize        removedSize = reader.ArraySize(removedObjects);
            ArchiveValue value{};
            for (usize t = 0; t < removedSize; ++t)
            {
                value = reader.ArrayNext(removedObjects, value);
                instance.removedObjects.Emplace(UUID::FromString(reader.StringValue(value)));
            }
        }

        {
            ArchiveValue removedComponents = reader.GetObjectValue(value, "removedComponents");
            usize        removedSize = reader.ArraySize(removedComponents);
            ArchiveValue value{};
            for (usize t = 0; t < removedSize; ++t)
            {
                value = reader.ArrayNext(removedComponents, value);
                instance.removedComponents.Emplace(UUID::FromString(reader.StringValue(value)));
            }
        }

        ArchiveValue arrChildren = reader.GetObjectValue(value, "children");
        usize        arrChildrenSize = reader.ArraySize(arrChildren);

        ArchiveValue vlChild{};
        for (usize c = 0; c < arrChildrenSize; ++c)
        {
            vlChild = reader.ArrayNext(arrChildren, vlChild);
            UUID childUUID   = UUID::FromString(reader.StringValue(reader.GetObjectValue(vlChild, "uuid")));
            GameObject* child = CreateInternal(childUUID, this);

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

                    if (UUID instanceComp = UUID::FromString(reader.StringValue(reader.GetObjectValue(vlComponent, "_instance"))); instanceComp && instance.object)
                    {
                        component->instance = instance.object->FindComponentByUUID(instanceComp);
                        instance.modifiedComponents.Emplace(uuid);
                    }

                    Serialization::Deserialize(typeHandler, reader, vlComponent, component);
                }
            }
        }

        if (instance.scene)
        {
            InitInstance();
        }
    }

    void GameObject::RemoveInstanceObject(GameObject* gameObject)
    {
        if (instance.object && gameObject->instance.object)
        {
            instance.removedObjects.Insert(gameObject->instance.object->uuid);
        }
    }

    void GameObject::RemoveInstanceComponent(Component* component)
    {
        if (instance.object && component->instance)
        {
            instance.removedComponents.Insert(component->instance->uuid);
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
