#pragma once
#include "Fyrion/Common.hpp"
#include "Fyrion/Core/Math.hpp"
#include "Fyrion/Scene/Proxy.hpp"

namespace Fyrion
{
    struct PhysicsContext;

    class FY_API PhysicsProxy final : public Proxy
    {
    public:
        FY_BASE_TYPES(Proxy);

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
