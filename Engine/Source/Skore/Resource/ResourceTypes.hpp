#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/Hash.hpp"

namespace Skore
{
    enum class ResourceFieldType
    {
        None = 0,
        Bool,
        Int,
        String,
        Float,
        RID,
        SubObject
    };


    struct RID
    {
        u64 id{};

        explicit operator bool() const noexcept
        {
            return this->id > 0;
        }

        bool operator==(const RID& rid) const
        {
            return this->id == rid.id;
        }

        bool operator!=(const RID& rid) const
        {
            return !(*this == rid);
        }
    };

    template<>
    struct Hash<RID>
    {
        constexpr static bool hasHash = true;
        constexpr static usize Value(const RID& rid)
        {
            return Hash<u64>::Value(rid.id);
        }
    };
}