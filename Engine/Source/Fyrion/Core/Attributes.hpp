#pragma once

#include "Fyrion/Common.hpp"
#include "String.hpp"

namespace Fyrion
{
    struct UIProperty {};

    struct UIFloatProperty
    {
        f32 minValue{};
        f32 maxValue{};
    };

    struct UIArrayProperty
    {
        bool canAdd = true;
        bool canRemove = true;
    };

    struct ProjectSettings;

    struct Settings
    {
        String path;
        TypeID type;
    };
}
