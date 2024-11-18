#pragma once

#include "Fyrion/Common.hpp"

namespace Fyrion {
    class GameObject;
}

namespace Fyrion
{
    class Scene;

    struct FY_API Proxy
    {
        Scene*  scene = nullptr;
        virtual ~Proxy() = default;

        virtual void OnStart() {}
        virtual void OnUpdate() {}
        virtual void OnDestroy() {}
        virtual void OnGameObjectStarted(GameObject* gameObject) {}
    };
}
