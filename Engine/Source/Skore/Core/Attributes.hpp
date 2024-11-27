#pragma once

#include "Skore/Common.hpp"
#include "String.hpp"

namespace Skore
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
