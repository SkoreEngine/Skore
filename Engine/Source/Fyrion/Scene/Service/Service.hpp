#pragma once

#include "Fyrion/Common.hpp"

namespace Fyrion
{
    struct FY_API Service
    {
        virtual ~Service() = default;

        virtual void OnStart() {}
        virtual void OnUpdate() {}
        virtual void OnDestroy() {}
    };
}
