#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Scene/Proxy.hpp"

namespace Skore
{
    struct PhysicsContext;

    class SK_API PhysicsProxy final : public Proxy
    {
    public:
        SK_BASE_TYPES(Proxy);

        void OnStart() override;
        void OnUpdate() override;
        void OnDestroy() override;

        void OnGameObjectStarted(GameObject* gameObject) override;
        void OnGameObjectDestroyed(GameObject* gameObject) override;

        void EnableSimulation();
        void DisableSimulation();


        void SetLinearAndAngularVelocity(GameObject* gameObject, const Vec3& linearVelocity, const Vec3& angularVelocity);

    private:
        bool                                                 simulationEnabled = false;
        f64                                                  accumulator = 0.0;
        PhysicsContext*                                      context = nullptr;
    };
}
