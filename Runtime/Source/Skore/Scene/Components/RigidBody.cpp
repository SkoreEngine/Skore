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

#include "RigidBody.hpp"

#include "Skore/Core/Reflection.hpp"


namespace Skore
{

    f32 RigidBody::GetMass() const
    {
        return m_mass;
    }

    void RigidBody::SetMass(f32 mass)
    {
        m_mass = mass;
    }

    f32 RigidBody::GetFriction() const
    {
        return m_friction;
    }

    void RigidBody::SetFriction(f32 friction)
    {
        m_friction = friction;
    }

    f32 RigidBody::GetRestitution() const
    {
        return m_restitution;
    }

    void RigidBody::SetRestitution(f32 restitution)
    {
        m_restitution = restitution;
    }

    f32 RigidBody::GetGravityFactor() const
    {
        return m_gravityFactor;
    }

    void RigidBody::SetGravityFactor(f32 gravityFactor)
    {
        m_gravityFactor = gravityFactor;
    }

    bool RigidBody::IsKinematic() const
    {
        return m_isKinematic;
    }

    void RigidBody::SetIsKinematic(bool isKinematic)
    {
        m_isKinematic = isKinematic;
    }

    CollisionDetectionType RigidBody::GetCollisionDetectionType() const
    {
        return m_collisionDetectionType;
    }

    void RigidBody::SetCollisionDetectionType(CollisionDetectionType collisionDetectionType)
    {
        m_collisionDetectionType = collisionDetectionType;
    }

    Vec3 RigidBody::GetLinearVelocity() const
    {
        return m_linearVelocity;
    }

    void RigidBody::SetLinearVelocity(const Vec3& linearVelocity)
    {
        this->m_linearVelocity = linearVelocity;
        // if (physicsProxy)
        // {
        //     physicsProxy->SetLinearAndAngularVelocity(gameObject, linearVelocity, angularVelocity);
        // }
    }

    Vec3 RigidBody::GetAngularVelocity() const
    {
        return m_angularVelocity;
    }

    void RigidBody::SetAngularVelocity(const Vec3& angularVelocity)
    {
        this->m_angularVelocity = angularVelocity;

        // if (physicsProxy)
        // {
        //     physicsProxy->SetLinearAndAngularVelocity(gameObject, linearVelocity, angularVelocity);
        // }
    }

	void RigidBody::RegisterType(NativeReflectType<RigidBody>& type)
	{
		type.Field<&RigidBody::m_mass>("mass");
		type.Field<&RigidBody::m_friction>("friction");
		type.Field<&RigidBody::m_restitution>("restitution");
		type.Field<&RigidBody::m_gravityFactor>("gravityFactor");
		type.Field<&RigidBody::m_isKinematic>("isKinematic");
		type.Field<&RigidBody::m_collisionDetectionType>("collisionDetectionType");

        type.Attribute<ComponentDesc>(ComponentDesc{.category = "Physics"});
	}
}
