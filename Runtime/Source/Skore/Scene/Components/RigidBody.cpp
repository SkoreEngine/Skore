#include "Skore/Scene/Components/RigidBody.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"


namespace Skore
{
    void RigidBody::OnCreate()
    {
        entity->AddFlag(EntityFlags::HasPhysics);
    }

    f32 RigidBody::GetMass() const
    {
        return m_mass;
    }

    void RigidBody::SetMass(f32 mass)
    {
        m_mass = mass;
        scene->physicsScene.PhysicsEntityRequireUpdate(entity);
    }

    f32 RigidBody::GetFriction() const
    {
        return m_friction;
    }

    void RigidBody::SetFriction(f32 friction)
    {
        m_friction = friction;
        scene->physicsScene.PhysicsEntityRequireUpdate(entity);
    }

    f32 RigidBody::GetRestitution() const
    {
        return m_restitution;
    }

    void RigidBody::SetRestitution(f32 restitution)
    {
        m_restitution = restitution;
        scene->physicsScene.PhysicsEntityRequireUpdate(entity);
    }

    f32 RigidBody::GetGravityFactor() const
    {
        return m_gravityFactor;
    }

    void RigidBody::SetGravityFactor(f32 gravityFactor)
    {
        m_gravityFactor = gravityFactor;
        scene->physicsScene.PhysicsEntityRequireUpdate(entity);
    }

    bool RigidBody::IsKinematic() const
    {
        return m_isKinematic;
    }

    void RigidBody::SetIsKinematic(bool isKinematic)
    {
        m_isKinematic = isKinematic;
        scene->physicsScene.PhysicsEntityRequireUpdate(entity);
    }

    CollisionDetectionType RigidBody::GetCollisionDetectionType() const
    {
        return m_collisionDetectionType;
    }

    void RigidBody::SetCollisionDetectionType(CollisionDetectionType collisionDetectionType)
    {
        m_collisionDetectionType = collisionDetectionType;
        scene->physicsScene.PhysicsEntityRequireUpdate(entity);
    }

    Vec3 RigidBody::GetLinearVelocity() const
    {
        return m_linearVelocity;
    }

    void RigidBody::SetLinearVelocity(const Vec3& linearVelocity)
    {
        m_linearVelocity = linearVelocity;
        scene->physicsScene.SetLinearVelocity(entity, linearVelocity);
    }

    Vec3 RigidBody::GetAngularVelocity() const
    {
        return m_angularVelocity;
    }

    void RigidBody::SetAngularVelocity(const Vec3& angularVelocity)
    {
        m_angularVelocity = angularVelocity;
        scene->physicsScene.SetAngularVelocity(entity, angularVelocity);
    }

    void RigidBody::AddForce(const Vec3& force)
    {
        scene->physicsScene.AddForce(entity, force);
    }

    void RigidBody::AddForceAtPosition(const Vec3& force, const Vec3& position)
    {
        scene->physicsScene.AddForceAtPosition(entity, force, position);
    }

    void RigidBody::AddImpulse(const Vec3& impulse)
    {
        scene->physicsScene.AddImpulse(entity, impulse);
    }

    void RigidBody::AddImpulseAtPosition(const Vec3& impulse, const Vec3& position)
    {
        scene->physicsScene.AddImpulseAtPosition(entity, impulse, position);
    }

    void RigidBody::AddTorque(const Vec3& torque)
    {
        scene->physicsScene.AddTorque(entity, torque);
    }

    void RigidBody::AddAngularImpulse(const Vec3& angularImpulse)
    {
        scene->physicsScene.AddAngularImpulse(entity, angularImpulse);
    }

	void RigidBody::RegisterType(NativeReflectType<RigidBody>& type)
	{
		type.Field<&RigidBody::m_mass>("mass");
		type.Field<&RigidBody::m_friction>("friction");
		type.Field<&RigidBody::m_restitution>("restitution");
		type.Field<&RigidBody::m_gravityFactor>("gravityFactor");
		type.Field<&RigidBody::m_isKinematic>("isKinematic");
		type.Field<&RigidBody::m_collisionDetectionType>("collisionDetectionType");

		type.Function<&RigidBody::GetLinearVelocity>("GetLinearVelocity");
		type.Function<&RigidBody::SetLinearVelocity>("SetLinearVelocity", "velocity");
		type.Function<&RigidBody::GetAngularVelocity>("GetAngularVelocity");
		type.Function<&RigidBody::SetAngularVelocity>("SetAngularVelocity", "velocity");
		type.Function<&RigidBody::AddForce>("AddForce", "force");
		type.Function<&RigidBody::AddForceAtPosition>("AddForceAtPosition", "force", "position");
		type.Function<&RigidBody::AddImpulse>("AddImpulse", "impulse");
		type.Function<&RigidBody::AddImpulseAtPosition>("AddImpulseAtPosition", "impulse", "position");
		type.Function<&RigidBody::AddTorque>("AddTorque", "torque");
		type.Function<&RigidBody::AddAngularImpulse>("AddAngularImpulse", "angularImpulse");

        type.Attribute<ComponentDesc>(ComponentDesc{.category = "Physics"});
	}
}