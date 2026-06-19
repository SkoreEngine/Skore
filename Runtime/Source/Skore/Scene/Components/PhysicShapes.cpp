#include "Skore/Scene/Components/PhysicShapes.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Physics.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
	void BoxCollider::OnCreate()
	{
		entity->AddFlag(EntityFlags::HasPhysics);
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	const Vec3& BoxCollider::GetSize() const
	{
		return m_size;
	}

	void BoxCollider::SetSize(const Vec3& halfSize)
	{
		m_size = halfSize;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	const Vec3& BoxCollider::GetCenter() const
	{
		return m_center;
	}

	void BoxCollider::SetCenter(const Vec3& center)
	{
		m_center = center;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}


	f32 BoxCollider::GetDensity() const
	{
		return m_density;
	}

	void BoxCollider::SetDensity(f32 density)
	{
		m_density = density;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	bool BoxCollider::IsSensor() const
	{
		return m_isSensor;
	}

	void BoxCollider::SetIsSensor(bool isSensor)
	{
		m_isSensor = isSensor;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
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
		else if (event.type == EntityEventType::DrawPhysicsShape)
		{
			DrawPhysicsShapeEvent* data = static_cast<DrawPhysicsShapeEvent*>(event.eventData);
			scene->physicsScene.DrawShape(entity, data->cmd, data->pipeline);
		}
	}

	void BoxCollider::RegisterType(NativeReflectType<BoxCollider>& type)
	{
		type.Field<&BoxCollider::m_isSensor, &BoxCollider::IsSensor, &BoxCollider::SetIsSensor>("isSensor");
		type.Field<&BoxCollider::m_density, &BoxCollider::GetDensity, &BoxCollider::SetDensity>("density");
		type.Field<&BoxCollider::m_center, &BoxCollider::GetCenter, &BoxCollider::SetCenter>("center");
		type.Field<&BoxCollider::m_size, &BoxCollider::GetSize, &BoxCollider::SetSize>("halfSize");

		type.Attribute<ComponentDesc>(ComponentDesc{.category = "Physics"});
	}

	// SphereCollider implementation
	void SphereCollider::OnCreate()
	{
		entity->AddFlag(EntityFlags::HasPhysics);
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	f32 SphereCollider::GetRadius() const
	{
		return m_radius;
	}

	void SphereCollider::SetRadius(f32 radius)
	{
		m_radius = radius;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	const Vec3& SphereCollider::GetCenter() const
	{
		return m_center;
	}

	void SphereCollider::SetCenter(const Vec3& center)
	{
		m_center = center;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	f32 SphereCollider::GetDensity() const
	{
		return m_density;
	}

	void SphereCollider::SetDensity(f32 density)
	{
		m_density = density;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	bool SphereCollider::IsSensor() const
	{
		return m_isSensor;
	}

	void SphereCollider::SetIsSensor(bool isSensor)
	{
		m_isSensor = isSensor;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	void SphereCollider::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::CollectPhysicsShapes)
		{
			ShapeCollector* collector = static_cast<ShapeCollector*>(event.eventData);

			collector->shapes.EmplaceBack(BodyShapeBuilder{
				.bodyShape = BodyShapeType::Sphere,
				.center = m_center,
				.radius = m_radius,
				.density = m_density,
				.sensor = m_isSensor
			});
		}
		else if (event.type == EntityEventType::DrawPhysicsShape)
		{
			DrawPhysicsShapeEvent* data = static_cast<DrawPhysicsShapeEvent*>(event.eventData);
			scene->physicsScene.DrawShape(entity, data->cmd, data->pipeline);
		}
	}

	void SphereCollider::RegisterType(NativeReflectType<SphereCollider>& type)
	{
		type.Field<&SphereCollider::m_isSensor, &SphereCollider::IsSensor, &SphereCollider::SetIsSensor>("isSensor");
		type.Field<&SphereCollider::m_density, &SphereCollider::GetDensity, &SphereCollider::SetDensity>("density");
		type.Field<&SphereCollider::m_center, &SphereCollider::GetCenter, &SphereCollider::SetCenter>("center");
		type.Field<&SphereCollider::m_radius, &SphereCollider::GetRadius, &SphereCollider::SetRadius>("radius");

		type.Attribute<ComponentDesc>(ComponentDesc{.category = "Physics"});
	}

	// CapsuleCollider implementation
	void CapsuleCollider::OnCreate()
	{
		entity->AddFlag(EntityFlags::HasPhysics);
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	f32 CapsuleCollider::GetRadius() const
	{
		return m_radius;
	}

	void CapsuleCollider::SetRadius(f32 radius)
	{
		m_radius = radius;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	f32 CapsuleCollider::GetHeight() const
	{
		return m_height;
	}

	void CapsuleCollider::SetHeight(f32 height)
	{
		m_height = height;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	const Vec3& CapsuleCollider::GetCenter() const
	{
		return m_center;
	}

	void CapsuleCollider::SetCenter(const Vec3& center)
	{
		m_center = center;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	f32 CapsuleCollider::GetDensity() const
	{
		return m_density;
	}

	void CapsuleCollider::SetDensity(f32 density)
	{
		m_density = density;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	bool CapsuleCollider::IsSensor() const
	{
		return m_isSensor;
	}

	void CapsuleCollider::SetIsSensor(bool isSensor)
	{
		m_isSensor = isSensor;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	void CapsuleCollider::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::CollectPhysicsShapes)
		{
			ShapeCollector* collector = static_cast<ShapeCollector*>(event.eventData);

			collector->shapes.EmplaceBack(BodyShapeBuilder{
				.bodyShape = BodyShapeType::Capsule,
				.center = m_center,
				.height = m_height,
				.radius = m_radius,
				.density = m_density,
				.sensor = m_isSensor
			});
		}
		else if (event.type == EntityEventType::DrawPhysicsShape)
		{
			DrawPhysicsShapeEvent* data = static_cast<DrawPhysicsShapeEvent*>(event.eventData);
			scene->physicsScene.DrawShape(entity, data->cmd, data->pipeline);
		}
	}

	void CapsuleCollider::RegisterType(NativeReflectType<CapsuleCollider>& type)
	{
		type.Field<&CapsuleCollider::m_isSensor, &CapsuleCollider::IsSensor, &CapsuleCollider::SetIsSensor>("isSensor");
		type.Field<&CapsuleCollider::m_density, &CapsuleCollider::GetDensity, &CapsuleCollider::SetDensity>("density");
		type.Field<&CapsuleCollider::m_center, &CapsuleCollider::GetCenter, &CapsuleCollider::SetCenter>("center");
		type.Field<&CapsuleCollider::m_radius, &CapsuleCollider::GetRadius, &CapsuleCollider::SetRadius>("radius");
		type.Field<&CapsuleCollider::m_height, &CapsuleCollider::GetHeight, &CapsuleCollider::SetHeight>("height");

		type.Attribute<ComponentDesc>(ComponentDesc{.category = "Physics"});
	}

	// CylinderCollider implementation
	void CylinderCollider::OnCreate()
	{
		entity->AddFlag(EntityFlags::HasPhysics);
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	f32 CylinderCollider::GetRadius() const
	{
		return m_radius;
	}

	void CylinderCollider::SetRadius(f32 radius)
	{
		m_radius = radius;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	f32 CylinderCollider::GetHeight() const
	{
		return m_height;
	}

	void CylinderCollider::SetHeight(f32 height)
	{
		m_height = height;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	const Vec3& CylinderCollider::GetCenter() const
	{
		return m_center;
	}

	void CylinderCollider::SetCenter(const Vec3& center)
	{
		m_center = center;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	f32 CylinderCollider::GetDensity() const
	{
		return m_density;
	}

	void CylinderCollider::SetDensity(f32 density)
	{
		m_density = density;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	bool CylinderCollider::IsSensor() const
	{
		return m_isSensor;
	}

	void CylinderCollider::SetIsSensor(bool isSensor)
	{
		m_isSensor = isSensor;
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	void CylinderCollider::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::CollectPhysicsShapes)
		{
			ShapeCollector* collector = static_cast<ShapeCollector*>(event.eventData);

			collector->shapes.EmplaceBack(BodyShapeBuilder{
				.bodyShape = BodyShapeType::Cylinder,
				.center = m_center,
				.height = m_height,
				.radius = m_radius,
				.density = m_density,
				.sensor = m_isSensor
			});
		}
		else if (event.type == EntityEventType::DrawPhysicsShape)
		{
			DrawPhysicsShapeEvent* data = static_cast<DrawPhysicsShapeEvent*>(event.eventData);
			scene->physicsScene.DrawShape(entity, data->cmd, data->pipeline);
		}
	}

	void CylinderCollider::RegisterType(NativeReflectType<CylinderCollider>& type)
	{
		type.Field<&CylinderCollider::m_isSensor, &CylinderCollider::IsSensor, &CylinderCollider::SetIsSensor>("isSensor");
		type.Field<&CylinderCollider::m_density, &CylinderCollider::GetDensity, &CylinderCollider::SetDensity>("density");
		type.Field<&CylinderCollider::m_center, &CylinderCollider::GetCenter, &CylinderCollider::SetCenter>("center");
		type.Field<&CylinderCollider::m_radius, &CylinderCollider::GetRadius, &CylinderCollider::SetRadius>("radius");
		type.Field<&CylinderCollider::m_height, &CylinderCollider::GetHeight, &CylinderCollider::SetHeight>("height");

		type.Attribute<ComponentDesc>(ComponentDesc{.category = "Physics"});
	}
}