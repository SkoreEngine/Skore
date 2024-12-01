#include "ResourceObject.hpp"

namespace Skore
{

    struct ResourceValue
    {
        union
        {
            u64  intValue;
            char buffer[20];
        };

        usize allocSize = {};
    };

    struct ResourceData
    {
        Array<ResourceValue> fields;
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
        data->fields[index].intValue = value;
    }

    void ResourceObject::SetString(u32 index, StringView value)
    {
        //if (value.Size() > bu)
        //data->fields[index].
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
