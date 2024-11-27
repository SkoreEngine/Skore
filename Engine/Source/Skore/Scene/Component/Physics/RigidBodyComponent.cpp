#include "RigidBodyComponent.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Physics/PhysicsProxy.hpp"
#include "Skore/Scene/GameObject.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Component/TransformComponent.hpp"

namespace Skore
{
    void RigidBodyComponent::OnStart()
    {
        physicsProxy = gameObject->GetScene()->GetProxy<PhysicsProxy>();
    }

    f32 RigidBodyComponent::GetMass() const
    {
        return mass;
    }

    void RigidBodyComponent::SetMass(f32 mass)
    {
        this->mass = mass;
    }

    f32 RigidBodyComponent::GetFriction() const
    {
        return friction;
    }

    void RigidBodyComponent::SetFriction(f32 friction)
    {
        this->friction = friction;
    }

    f32 RigidBodyComponent::GetRestitution() const
    {
        return restitution;
    }

    void RigidBodyComponent::SetRestitution(f32 restitution)
    {
        this->restitution = restitution;
    }

    f32 RigidBodyComponent::GetGravityFactor() const
    {
        return gravityFactor;
    }

    void RigidBodyComponent::SetGravityFactor(f32 gravityFactor)
    {
        this->gravityFactor = gravityFactor;
    }

    bool RigidBodyComponent::IsKinematic() const
    {
        return isKinematic;
    }

    void RigidBodyComponent::SetIsKinematic(bool isKinematic)
    {
        this->isKinematic = isKinematic;
    }

    CollisionDetectionType RigidBodyComponent::GetCollisionDetectionType() const
    {
        return collisionDetectionType;
    }

    void RigidBodyComponent::SetCollisionDetectionType(CollisionDetectionType collisionDetectionType)
    {
        this->collisionDetectionType = collisionDetectionType;
    }

    Vec3 RigidBodyComponent::GetLinearVelocity() const
    {
        return linearVelocity;
    }

    void RigidBodyComponent::SetLinearVelocity(const Vec3& linearVelocity)
    {
        this->linearVelocity = linearVelocity;
        if (physicsProxy)
        {
            physicsProxy->SetLinearAndAngularVelocity(gameObject, linearVelocity, angularVelocity);
        }
    }

    Vec3 RigidBodyComponent::GetAngularVelocity() const
    {
        return angularVelocity;
    }

    void RigidBodyComponent::SetAngularVelocity(const Vec3& angularVelocity)
    {
        this->angularVelocity = angularVelocity;
        if (physicsProxy)
        {
            physicsProxy->SetLinearAndAngularVelocity(gameObject, linearVelocity, angularVelocity);
        }
    }

    void RigidBodyComponent::RegisterType(NativeTypeHandler<RigidBodyComponent>& type)
    {
        type.Field<&RigidBodyComponent::mass>("mass").Attribute<UIProperty>();
        type.Field<&RigidBodyComponent::friction>("friction").Attribute<UIProperty>();
        type.Field<&RigidBodyComponent::restitution>("restitution").Attribute<UIProperty>();
        type.Field<&RigidBodyComponent::gravityFactor>("gravityFactor").Attribute<UIProperty>();
        type.Field<&RigidBodyComponent::isKinematic>("isKinematic").Attribute<UIProperty>();
        type.Field<&RigidBodyComponent::collisionDetectionType>("collisionDetectionType").Attribute<UIProperty>();

        type.Attribute<ComponentDesc>(ComponentDesc{.dependencies = {GetTypeID<TransformComponent>()}});
    }
}

