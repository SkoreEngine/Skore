#pragma once

#include "Skore/Common.hpp"

namespace Skore
{
    class Scene;
    class GameObject;

    struct SK_API Proxy
    {
        Scene*  scene = nullptr;
        virtual ~Proxy() = default;

        virtual void OnStart() {}
        virtual void OnUpdate() {}
        virtual void OnDestroy() {}
        virtual void OnGameObjectStarted(GameObject* gameObject) {}
        virtual void OnGameObjectDestroyed(GameObject* gameObject) {}
    };
}
