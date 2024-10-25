#pragma once

#include "Fyrion/Common.hpp"

namespace Fyrion
{
    class Scene;

    struct FY_API Service
    {
        Scene*  scene = nullptr;
        virtual ~Service() = default;

        virtual void OnStart() {}
        virtual void OnUpdate() {}
        virtual void OnDestroy() {}
    };
}
