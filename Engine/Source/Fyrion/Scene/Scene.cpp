#include "Scene.hpp"

#include "Fyrion/Core/Registry.hpp"

namespace Fyrion
{

    Scene::Scene()
    {
        services = Registry::InstantiateDerivedAsMap<Service>();
    }

    Scene::~Scene()
    {
        for(auto& it: services)
        {
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
        for (GameObject* gameObject : queueToDestroy)
        {
            MemoryGlobals::GetDefaultAllocator().DestroyAndFree(gameObject);
        }
        queueToDestroy.Clear();
    }

    void Scene::DoUpdate()
    {
        for(auto& it: services)
        {
            it.second->OnUpdate();
        }
        FlushQueues();
    }

    Service* Scene::GetService(TypeID typeId)
    {
        if (auto it = services.Find(typeId))
        {
            return it->second;
        }
        return nullptr;
    }

    GameObject& Scene::GetRootObject()
    {
        return root;
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
