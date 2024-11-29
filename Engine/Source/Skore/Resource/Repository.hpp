#pragma once

#include "ResourceObject.hpp"
#include "ResourceTypes.hpp"
#include "Skore/Core/TypeID.hpp"

namespace Skore
{
    class SK_API Repository
    {
    public:
        static TransactionScope* CreateTransactionScope();

        static RID            CreateObjectOf(TypeID typeId, TransactionScope* transactionScope = nullptr);
        static RID            CreateObjectFrom(RID rid, TransactionScope* transactionScope = nullptr);
        static ResourceObject Write(RID rid);
        static ResourceObject Read(RID rid);

        static VoidPtr Instantiate(RID rid);

        template <typename T>
        static RID CreateObjectOf()
        {
            return CreateObjectOf(GetTypeID<T>());
        }

        template <typename T>
        static T* Instantiate(RID rid)
        {
            return static_cast<T*>(Instantiate(rid));
        }
    };
}
