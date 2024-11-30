#pragma once

#include "ResourceTypes.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
    struct ResourceObjectInternal;
    class TransactionScope;


    class SK_API ResourceObject
    {
    public:
        u32 GetIndex(StringView name);

        ResourceFieldType GetType(u32 index) const;

        void SetInt(u32 index, i64 value);
        void SetString(u32 index, StringView value);
        void SetVec2(u32 index, Vec2 value);
        void SetVec3(u32 index, Vec3 value);
        void SetVec4(u32 index, Vec4 value);
        void SetQuat(u32 index, Quat value);

        i64  GetInt(u32 index) const;

        void Commit(TransactionScope* transactionScope = nullptr);

        ~ResourceObject();
    private:
        ResourceObjectInternal* internal = nullptr;
    };
}
