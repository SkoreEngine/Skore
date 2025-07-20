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

#include "PhysicShapes.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Physics.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	void BoxCollider::Create(ComponentSettings& settings)
	{
		entity->AddFlag(EntityFlags::HasPhysics);
	}

	const Vec3& BoxCollider::GetSize() const
	{
		return m_size;
	}

	void BoxCollider::SetSize(const Vec3& halfSize)
	{
		this->m_size = halfSize;
		GetScene()->GetPhysicsScene()->PhysicsEntityRequireUpdate(entity);
	}

	const Vec3& BoxCollider::GetCenter() const
	{
		return m_center;
	}

	void BoxCollider::SetCenter(const Vec3& center)
	{
		this->m_center = center;
		GetScene()->GetPhysicsScene()->PhysicsEntityRequireUpdate(entity);
	}


	f32 BoxCollider::GetDensity() const
	{
		return m_density;
	}

	void BoxCollider::SetDensity(f32 density)
	{
		this->m_density = density;
		GetScene()->GetPhysicsScene()->PhysicsEntityRequireUpdate(entity);
	}

	bool BoxCollider::IsSensor() const
	{
		return m_isSensor;
	}

	void BoxCollider::SetIsSensor(bool isSensor)
	{
		this->m_isSensor = isSensor;
		GetScene()->GetPhysicsScene()->PhysicsEntityRequireUpdate(entity);
	}

	void BoxCollider::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::CollectPhysicsShapes)
		{
			ShapeCollector* collector = static_cast<ShapeCollector*>(event.eventData);

			collector->shapes.EmplaceBack(BodyShapeBuilder{
				.bodyShape = BodyShapeType::Box,
				.size = m_size,
				.center = m_center,
				.density = m_density,
				.sensor = m_isSensor
			});
		}
	}

	void BoxCollider::RegisterType(NativeReflectType<BoxCollider>& type)
	{
		type.Field<&BoxCollider::m_isSensor, &IsSensor, &SetIsSensor>("isSensor");
		type.Field<&BoxCollider::m_density, &GetDensity, &SetDensity>("density");
		type.Field<&BoxCollider::m_center, &GetCenter, &SetCenter>("center");
		type.Field<&BoxCollider::m_size, &GetSize, &SetSize>("halfSize");

		type.Attribute<ComponentDesc>(ComponentDesc{.category = "Physics"});
	}
}
