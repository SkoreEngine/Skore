#pragma once

#include "GameObject.hpp"
#include "SceneTypes.hpp"
#include "Fyrion/Common.hpp"
#include "Fyrion/Core/HashMap.hpp"
#include "Fyrion/IO/Asset.hpp"
#include "Service/Service.hpp"

namespace Fyrion
{
    class FY_API Scene : public Asset
    {
    public:
        FY_BASE_TYPES(Asset);

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

        Service* GetService(TypeID typeId);

        template<typename T>
        T* GetService()
        {
            return static_cast<T*>(GetService(GetTypeID<T>()));
        }

        friend class GameObject;

        usize GetObjectCount() const
        {
            return objectsById.Size();
        }

    private:
        GameObject                 root = {this};
        HashMap<UUID, GameObject*> objectsById;
        HashMap<TypeID, Service*>  services;

        Array<GameObject*> queueToDestroy;
        Array<GameObject*> queueToStart;
        Array<Component*>  componentsToStart;
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
