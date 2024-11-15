#include "GameObject.hpp"
#include "Scene.hpp"
#include "Component/Component.hpp"
#include "Fyrion/Core/Logger.hpp"
#include "Fyrion/Core/Registry.hpp"

namespace Fyrion
{
    namespace
    {
        Logger& logger = Logger::GetLogger("Fyrion::GameObject");
    }

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
        if (parent == nullptr && scene != nullptr)
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
        if (parent == nullptr && scene != nullptr)
        {
            return scene->GetUUID();
        }
        return uuid;
    }

    Span<GameObject*> GameObject::GetChildren() const
    {
        return children;
    }

    GameObject* GameObject::CreateInternal(UUID uuid, GameObject* parent) const
    {
        GameObject* gameObject = new(MemoryGlobals::GetDefaultAllocator().MemAlloc(
            sizeof(GameObject), alignof(GameObject))) GameObject{parent != nullptr ? parent->scene : nullptr, parent};

        gameObject->uuid = uuid;

        if (parent)
        {
            parent->children.EmplaceBack(gameObject);

            if (parent->scene)
            {
                scene->objectsById.Insert(uuid, gameObject);
                scene->queueToStart.EmplaceBack(gameObject);
            }
        }

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

    GameObject* GameObject::FindChildByPrefab(UUID uuid) const
    {
        for (GameObject* child : children)
        {
            if (child->prefab.object && child->prefab.object->GetUUID() == uuid)
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

    GameObject* GameObject::GetPrefabObject(UUID uuid) const
    {
        if (parent->prefab.object == nullptr)
        {
            if (Scene* prefabScene = Assets::Load<Scene>(uuid))
            {
                return &prefabScene->GetRootObject();
            }
        }
        else
        {
            return parent->prefab.object->GetScene()->FindObjectByUUID(uuid);
        }

        return nullptr;
    }

    GameObject* GameObject::Create()
    {
        return CreateInternal(UUID::RandomUUID(), this);
    }

    GameObject* GameObject::Create(UUID uuid)
    {
        return CreateInternal(uuid, this);
    }

    GameObject* GameObject::Duplicate() const
    {
        return Duplicate(parent);
    }

    GameObject* GameObject::Duplicate(GameObject* parent) const
    {
        GameObject* gameObject = CreateInternal(UUID::RandomUUID(), parent);
        gameObject->SetName(GetName());
        gameObject->prefab = prefab;

        for (Component* component : components)
        {
            Component* newComponent = gameObject->AddComponent(component->typeHandler, UUID::RandomUUID());
            newComponent->typeHandler->DeepCopy(component, newComponent);
            newComponent->instance = component->instance;
        }

        for (GameObject* child : children)
        {
            child->Duplicate(gameObject);
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

    GameObject* GameObject::FindChildByUUID(UUID uuid) const
    {
        for (GameObject* child : children)
        {
            if (child->uuid == uuid)
            {
                return child;
            }
        }
        return nullptr;
    }


    //TODO cache index?
    usize GameObject::GetIndex() const
    {
        if (parent == nullptr)
        {
            return nPos;
        }
        return FindFirstIndex(parent->children.begin(), parent->children.end(), const_cast<GameObject*>(this));
    }

    void GameObject::MoveTo(usize index)
    {
        if (parent == nullptr)
        {
            return;
        }

        auto oldIndex = FindFirstIndex(parent->children.begin(), parent->children.end(), this);

        if (oldIndex == index)
        {
            return;
        }

        if (oldIndex == nPos)
        {
            return;
        }

        parent->children.Erase(parent->children.begin() + oldIndex);

        if (index > parent->children.Size() || index == U32_MAX)
        {
            parent->children.EmplaceBack(this);
        }
        else
        {
            usize newIndex = oldIndex > index ? index : index - 1;
            parent->children.Insert(parent->children.begin() + newIndex, this);
        }
    }

    void GameObject::SetParent(GameObject* newParent)
    {
        if (newParent == nullptr)
        {
            logger.Error("parent cannot be null");
            return;
        }

        if (this->IsParentOf(newParent))
        {
            logger.Error("object is parent of {} ", newParent->GetName());
            return;
        }

        parent->RemoveChild(this);
        this->parent = newParent;

        if (this->scene != nullptr && this->scene != newParent->scene)
        {
            this->scene->objectsById.Erase(this->GetUUID());
            newParent->scene->objectsById.Insert(this->GetUUID(), this);
        }

        this->scene = newParent->scene;
        this->parent->children.EmplaceBack(this);
    }

    bool GameObject::IsParentOf(GameObject* object) const
    {
        if (object->GetParent() == nullptr)
        {
            return false;
        }

        if (this == object->GetParent())
        {
            return true;
        }

        return this->IsParentOf(object->GetParent());
    }

    void GameObject::SetPrefab(GameObject* gameObject)
    {
        if (gameObject)
        {
            if (!parent)
            {
                logger.Error("prefabs cannot be set on the root entity");
                return;
            }

            prefab.object = gameObject;
            SetName(prefab.object->GetName());

            for (Component* component : prefab.object->components)
            {
                Component* newComponent = AddComponent(component->typeHandler, UUID::RandomUUID());
                newComponent->typeHandler->DeepCopy(component, newComponent);
                newComponent->instance = component;
            }

            for (GameObject* child : prefab.object->children)
            {
                GameObject* childPrefab = this->Create();
                childPrefab->SetPrefab(child);
            }
        }
        else if (prefab.object != nullptr)
        {
            //TODO remove prefab?
        }
    }

    void GameObject::SetPrefab(UUID prefabId)
    {
        if (prefabId)
        {
            GameObject* gameObject = GetPrefabObject(prefabId);
            if (!gameObject)
            {
                logger.Error("prefab id {} not found", prefabId.ToString());
                return;
            }
            SetPrefab(gameObject);
        }
        else
        {
            SetPrefab(nullptr);
        }
    }

    GameObject* GameObject::GetPrefab() const
    {
        return prefab.object;
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
        if (started && scene)
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
        if (component->instance)
        {
            prefab.modifiedComponents.Emplace(component->instance->uuid);
        }
    }

    void GameObject::RemoveComponentOverride(Component* component, bool resetValue)
    {
        if (component->instance)
        {
            prefab.modifiedComponents.Erase(component->instance->uuid);
            if (resetValue)
            {
                component->typeHandler->DeepCopy(component->instance, component);
                component->OnChange();
            }
        }
    }

    bool GameObject::IsComponentOverride(Component* component)
    {
        if (component->instance)
        {
            return prefab.modifiedComponents.Has(component->instance->uuid);
        }
        return false;
    }

    ArchiveValue GameObject::Serialize(ArchiveWriter& writer) const
    {
        ArchiveValue object = writer.CreateObject();

        if (!name.Empty() && (!prefab.object || prefab.object->name != name))
        {
            writer.AddToObject(object, "name", writer.StringValue(name));
        }

        if (uuid)
        {
            writer.AddToObject(object, "uuid", writer.StringValue(uuid.ToString()));
        }

        if (prefab.object && prefab.object->GetUUID())
        {
            writer.AddToObject(object, "prefab", writer.StringValue(prefab.object->GetUUID().ToString()));
        }

        if (!prefab.removedObjects.Empty())
        {
            ArchiveValue removedArr = writer.CreateArray();
            for (auto& it : prefab.removedObjects)
            {
                writer.AddToArray(removedArr, writer.StringValue(it.first.ToString()));
            }
            writer.AddToObject(object, "removedObjects", removedArr);
        }

        if (!prefab.removedComponents.Empty())
        {
            ArchiveValue removedArr = writer.CreateArray();
            for (auto& it : prefab.removedComponents)
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
            if (component->instance != nullptr && !prefab.modifiedComponents.Has(component->instance->uuid)) continue;

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

        {
            ArchiveValue removedObjects = reader.GetObjectValue(value, "removedObjects");
            usize        removedSize = reader.ArraySize(removedObjects);
            ArchiveValue value{};
            for (usize t = 0; t < removedSize; ++t)
            {
                value = reader.ArrayNext(removedObjects, value);
                prefab.removedObjects.Emplace(UUID::FromString(reader.StringValue(value)));
            }
        }

        {
            ArchiveValue removedComponents = reader.GetObjectValue(value, "removedComponents");
            usize        removedSize = reader.ArraySize(removedComponents);
            ArchiveValue value{};
            for (usize t = 0; t < removedSize; ++t)
            {
                value = reader.ArrayNext(removedComponents, value);
                prefab.removedComponents.Emplace(UUID::FromString(reader.StringValue(value)));
            }
        }

        if (UUID prefabId = UUID::FromString(reader.StringValue(reader.GetObjectValue(value, "prefab"))))
        {
            prefab.object = GetPrefabObject(prefabId);

            if (name.Empty())
            {
                SetName(prefab.object->GetName());
            }
        }

        ArchiveValue arrChildren = reader.GetObjectValue(value, "children");
        usize        arrChildrenSize = reader.ArraySize(arrChildren);

        ArchiveValue vlChild{};
        for (usize c = 0; c < arrChildrenSize; ++c)
        {
            vlChild = reader.ArrayNext(arrChildren, vlChild);
            UUID        childUUID = UUID::FromString(reader.StringValue(reader.GetObjectValue(vlChild, "uuid")));
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
                    UUID       uuid = UUID::FromString(reader.StringValue(reader.GetObjectValue(vlComponent, "_uuid")));
                    Component* component = AddComponent(typeHandler, uuid);

                    if (UUID instanceComp = UUID::FromString(reader.StringValue(reader.GetObjectValue(vlComponent, "_instance"))); instanceComp && prefab.object)
                    {
                        component->instance = prefab.object->FindComponentByUUID(instanceComp);
                        prefab.modifiedComponents.Emplace(instanceComp);
                    }

                    Serialization::Deserialize(typeHandler, reader, vlComponent, component);
                }
            }
        }

        if (prefab.object)
        {
            for (Component* component : prefab.object->components)
            {
                if (prefab.modifiedComponents.Has(component->uuid)) continue;

                Component* newComponent = AddComponent(component->typeHandler, UUID::RandomUUID());
                newComponent->typeHandler->DeepCopy(component, newComponent);
                newComponent->instance = component;
            }
        }
    }

    void GameObject::RemovePrefabObject(GameObject* gameObject)
    {
        if (prefab.object && gameObject->prefab.object)
        {
            prefab.removedObjects.Insert(gameObject->prefab.object->uuid);
        }
    }

    void GameObject::RemovePrefabComponent(Component* component)
    {
        if (prefab.object && component->instance)
        {
            prefab.removedComponents.Insert(component->instance->uuid);
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
        if (scene)
        {
            scene->DestroyGameObject(this);
        }
        else
        {
            MemoryGlobals::GetDefaultAllocator().DestroyAndFree(this);
        }
    }
}
