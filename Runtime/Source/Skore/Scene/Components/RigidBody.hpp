#pragma once
#include "Skore/Scene/Component.hpp"
#include "Skore/Scene/Physics.hpp"


namespace Skore
{
	class SK_API RigidBody : public Component
	{
	public:
		SK_CLASS(RigidBody, Component);

		void Create() override;

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

		void AddForce(const Vec3& force);
		void AddForceAtPosition(const Vec3& force, const Vec3& position);
		void AddImpulse(const Vec3& impulse);
		void AddImpulseAtPosition(const Vec3& impulse, const Vec3& position);
		void AddTorque(const Vec3& torque);
		void AddAngularImpulse(const Vec3& angularImpulse);

		static void RegisterType(NativeReflectType<RigidBody>& type);

		friend class PhysicsScene;
	private:
		f32  m_mass = 1.0f;
		f32  m_friction = 0.6f;
		f32  m_restitution = 0.6f;
		f32  m_gravityFactor = 1.0;
		bool m_isKinematic = false;
		Vec3 m_linearVelocity{};
		Vec3 m_angularVelocity{};

		CollisionDetectionType m_collisionDetectionType = CollisionDetectionType::Discrete;
	};
}
