// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once
#include "Skore/Scene/Component.hpp"
#include "Skore/Scene/Physics.hpp"


namespace Skore
{
	class SK_API RigidBody : public Component
	{
	public:
		SK_CLASS(RigidBody, Component);

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

		static void RegisterType(NativeReflectType<RigidBody>& type);

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
