#pragma once

#include "GameObject.hpp"
#include "Skore/Common.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/UnorderedDense.hpp"
#include "Skore/IO/Asset.hpp"
#include "Proxy.hpp"

namespace Skore
{
    class SK_API Scene : public Asset
    {
    public:
        SK_BASE_TYPES(Asset);

        ~Scene() override;
        Scene();

        static void RegisterType(NativeTypeHandler<Scene>& type);

        GameObject& GetRootObject();
        GameObject* FindObjectByUUID(UUID uuid) const;

        ArchiveValue Serialize(ArchiveWriter& writer) const;
        void         Deserialize(ArchiveReader& reader, ArchiveValue value);

        void DestroyGameObject(GameObject* gameObject);

        void FlushQueues();
        void Update();
        void Start();

        Proxy* GetProxy(TypeID typeId);

        template <typename T>
        T* GetProxy()
        {
            return static_cast<T*>(GetProxy(GetTypeID<T>()));
        }

        usize GetObjectCount() const
        {
            return objectsById.Size();
        }

        bool IsDestroyed() const
        {
            return destroyed;
        }

        friend class GameObject;
        friend class Component;

    private:
        GameObject                 root = {this};
        HashMap<UUID, GameObject*> objectsById;
        HashMap<TypeID, Proxy*>    proxies;

        Array<GameObject*> queueToDestroy;
        Array<GameObject*> queueToStart;
        Array<Component*>  componentsToStart;

        bool destroyed = false;

        ankerl::unordered_dense::set<Component*> componentsToUpdate;
    };


    template <>
    struct ArchiveType<Scene>
    {
        constexpr static bool hasArchiveImpl = true;

        static ArchiveValue ToValue(ArchiveWriter& writer, const Scene& value)
        {
            return value.Serialize(writer);
        }

        static void FromValue(ArchiveReader& reader, ArchiveValue archiveValue, Scene& typeValue)
        {
            typeValue.Deserialize(reader, archiveValue);
        }
    };
}
