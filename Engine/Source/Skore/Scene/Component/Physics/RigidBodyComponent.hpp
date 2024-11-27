#pragma once
#include "Skore/Physics/PhysicsTypes.hpp"
#include "Skore/Scene/Component/Component.hpp"

namespace Skore
{
    class PhysicsProxy;
}

namespace Skore
{
    class SK_API RigidBodyComponent : public Component
    {
    public:
        SK_BASE_TYPES(Component);

        void OnStart() override;

        f32                    GetMass() const;
        void                   SetMass(f32 mass);
        f32                    GetFriction() const;
        void                   SetFriction(f32 friction);
        f32                    GetRestitution() const;
        void                   SetRestitution(f32 restitution);
        f32                    GetGravityFactor() const;
        void                   SetGravityFactor(f32 gravityFactor);
        bool                   IsKinematic() const;
        void                   SetIsKinematic(bool isKinematic);
        CollisionDetectionType GetCollisionDetectionType() const;
        void                   SetCollisionDetectionType(CollisionDetectionType collisionDetectionType);
        Vec3                   GetLinearVelocity() const;
        void                   SetLinearVelocity(const Vec3& linearVelocity);
        Vec3                   GetAngularVelocity() const;
        void                   SetAngularVelocity(const Vec3& angularVelocity);

        static void RegisterType(NativeTypeHandler<RigidBodyComponent>& type);

        friend class PhysicsProxy;
    private:
        f32                    mass = 1.0f;
        f32                    friction = 0.6f;
        f32                    restitution = 0.6f;
        f32                    gravityFactor = 1.0;
        bool                   isKinematic = false;
        CollisionDetectionType collisionDetectionType = CollisionDetectionType::Discrete;

        Vec3 linearVelocity{};
        Vec3 angularVelocity{};

        PhysicsProxy* physicsProxy = nullptr;
    };
}
