#include "ResourceObject.hpp"

namespace Skore
{
    u32 ResourceObject::GetIndex(StringView name)
    {
        return 0;
    }

    ResourceFieldType ResourceObject::GetType(u32 index) const
    {
        return ResourceFieldType::None;
    }


    void ResourceObject::SetInt(u32 index, i64 value) {}
    void ResourceObject::SetString(u32 index, StringView value) {}
    void ResourceObject::SetVec2(u32 index, Vec2 value) {}
    void ResourceObject::SetVec3(u32 index, Vec3 value) {}
    void ResourceObject::SetVec4(u32 index, Vec4 value) {}
    void ResourceObject::SetQuat(u32 index, Quat value) {}

    i64 ResourceObject::GetInt(u32 index) const
    {
        return 0;
    }


    void ResourceObject::Commit(TransactionScope* scope) {}

    ResourceObject::~ResourceObject()
    {
        //TODO
    }
}
