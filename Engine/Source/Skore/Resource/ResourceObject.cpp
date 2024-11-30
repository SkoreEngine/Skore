#include "ResourceObject.hpp"

namespace Skore
{

    struct ResourceObjectValue
    {
        union
        {
            u64 intValue;
        };
    };

    struct ResourceObjectStorage
    {

    };

    u32 ResourceObject::GetIndex(StringView name)
    {
        return 0;
    }

    ResourceFieldType ResourceObject::GetType(u32 index) const
    {
        return ResourceFieldType::None;
    }


    void ResourceObject::SetInt(u32 index, i64 value)
    {
        SK_ASSERT(data, "data is null");

     //   data->fields[]
    }

    void ResourceObject::SetString(u32 index, StringView value)
    {

    }

    i64 ResourceObject::GetInt(u32 index) const
    {
        return 0;
    }


    void ResourceObject::Commit(TransactionScope* scope)
    {

    }

    ResourceObject::~ResourceObject()
    {
        //TODO
    }
}
