#pragma once

#include "ResourceObject.hpp"
#include "ResourceTypes.hpp"
#include "Skore/Core/TypeID.hpp"
#include "Skore/Core/UUID.hpp"

namespace Skore
{
    class SK_API Repository
    {
    public:
        static TransactionScope* CreateTransactionScope();

        static RID            CreateObjectOf(TypeID typeId, TransactionScope* transactionScope = nullptr);
        static RID            CreateObjectFrom(RID rid, TransactionScope* transactionScope = nullptr);
        static void           RegisterHandler(RID rid, UUID uuid, ResourceHandler* resourceHandler);
        static void           SetPath(RID rid, StringView path);
        static RID            GetFromUUID(UUID uuid);
        static RID            GetFromPath(StringView path);
        static ResourceObject Write(RID rid);
        static ResourceObject Read(RID rid);

        static VoidPtr Instantiate(RID rid);

        template <typename T>
        static RID CreateObjectOf(TransactionScope* transactionScope = nullptr)
        {
            return CreateObjectOf(GetTypeID<T>(), transactionScope);
        }

        template <typename T>
        static T* Instantiate(RID rid)
        {
            return static_cast<T*>(Instantiate(rid));
        }


        //temporary

        static void NewResourceSystemEnabled();
        static bool IsNewResourceSystemEnabled();
    };
}
