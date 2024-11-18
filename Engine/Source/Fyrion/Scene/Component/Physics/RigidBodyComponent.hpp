#pragma once
#include "Fyrion/Physics/PhysicsTypes.hpp"
#include "Fyrion/Scene/Component/Component.hpp"

namespace Fyrion
{
    class FY_API RigidBodyComponent : public Component
    {
    public:
        FY_BASE_TYPES(Component);

        void OnStart() override;

        static void RegisterType(NativeTypeHandler<RigidBodyComponent>& type);

    private:
        f32                    mass = 1.0f;
        f32                    friction = 0.6f;
        f32                    restitution = 0.6f;
        f32                    gravityFactor = 1.0;
        bool                   isKinematic = false;
        CollisionDetectionType collisionDetectionType = CollisionDetectionType::Discrete;
    };
}
