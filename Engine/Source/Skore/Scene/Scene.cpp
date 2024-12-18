#include "Scene.hpp"

#include "Component/Component.hpp"
#include "Skore/Core/Registry.hpp"

namespace Skore
{

    Scene::Scene()
    {
        proxies = Registry::InstantiateDerivedAsMap<Proxy>();
        for(auto it  : proxies)
        {
            it.second->scene = this;
        }
    }

    Scene::~Scene()
    {
        for(auto& it: proxies)
        {
            it.second->OnDestroy();
            if (TypeHandler* handler = Registry::FindTypeById(it.first))
            {
                handler->Destroy(it.second);
            }
        }
    }

    void Scene::DestroyGameObject(GameObject* gameObject)
    {
        queueToDestroy.EmplaceBack(gameObject);
    }

    void Scene::FlushQueues()
    {
        for(GameObject* gameObject : queueToStart)
        {
            gameObject->Start();
            for(auto& it: proxies)
            {
                it.second->OnGameObjectStarted(gameObject);
            }
        }
        queueToStart.Clear();


        for(Component* component : componentsToStart)
        {
            component->OnStart();
        }

        componentsToStart.Clear();

        for (GameObject* gameObject : queueToDestroy)
        {
            MemoryGlobals::GetDefaultAllocator().DestroyAndFree(gameObject);
        }
        queueToDestroy.Clear();
    }

    void Scene::Update()
    {
        for(auto& it: proxies)
        {
            it.second->OnUpdate();
        }
        FlushQueues();

        for (Component* component : componentsToUpdate)
        {
            component->OnUpdate();
        }
    }

    void Scene::Start()
    {
        for(auto& it: proxies)
        {
            it.second->OnStart();
        }
    }

    Proxy* Scene::GetProxy(TypeID typeId)
    {
        if (auto it = proxies.Find(typeId))
        {
            return it->second;
        }
        return nullptr;
    }

    GameObject& Scene::GetRootObject()
    {
        return root;
    }

    GameObject* Scene::FindObjectByUUID(UUID uuid) const
    {
        if (auto it = objectsById.Find(uuid))
        {
            return it->second;
        }

        if (GetUUID() == uuid)
        {
            return const_cast<GameObject*>(&root);
        }

        return nullptr;
    }

    ArchiveValue Scene::Serialize(ArchiveWriter& writer) const
    {
        ArchiveValue sceneValue = writer.CreateObject();
        writer.AddToObject(sceneValue, "root", root.Serialize(writer));
        return sceneValue;
    }

    void Scene::Deserialize(ArchiveReader& reader, ArchiveValue value)
    {
        root.Deserialize(reader, reader.GetObjectValue(value, "root"));
    }

    void Scene::RegisterType(NativeTypeHandler<Scene>& type)
    {

    }
}
