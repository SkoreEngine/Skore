#pragma once

#include "ResourceTypes.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
    struct ResourceObjectStorage;
    class TransactionScope;


    class SK_API ResourceObject
    {
    public:
        u32 GetIndex(StringView name);

        ResourceFieldType GetType(u32 index) const;

        void SetBool(u32 index, bool value);
        void SetInt(u32 index, i64 value);
        void SetUInt(u32 index, u64 value);
        void SetFloat(u32 index, f64 value);
        void SetString(u32 index, StringView value);

        bool       GetBool(u32 index) const;
        i64        GetInt(u32 index) const;
        u64        GetUInt(u32 index) const;
        f64        GetFloat(u32 index) const;
        StringView GetString(u32 index) const;


        void Commit(TransactionScope* transactionScope = nullptr);

        ~ResourceObject();
    private:
        ResourceObjectStorage* data = nullptr;
    };
}
