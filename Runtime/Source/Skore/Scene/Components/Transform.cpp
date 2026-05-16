#include "Skore/Scene/Components/Transform.hpp"

#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Physics.hpp"
#include "Skore/Scene/Scene.hpp"


namespace Skore
{
	const Mat4& Transform::GetParentWorldTransform() const
	{
		if (entity->GetParent() != nullptr)
		{
			return entity->GetParent()->GetWorldTransform();
		}
		return IdentityMatrix;
	}

	void Transform::UpdateTransform(u32 flags)
	{
		entity->SetWorldTransform(GetParentWorldTransform() * GetLocalTransform());

		EntityEventDesc desc;
		desc.type = EntityEventType::TransformUpdated;
		desc.flags = flags;

		for (Component* component : entity->GetComponents())
		{
			component->ProcessEvent(desc);
		}

		desc.type = EntityEventType::ParentTransformUpdated;

		for (Entity* child : entity->GetChildren())
		{
			child->NotifyEvent(desc, false);
		}
	}

	void Transform::ProcessEvent(const EntityEventDesc& event)
	{
		if (event.type == EntityEventType::ParentTransformUpdated || event.type == EntityEventType::EntityParentChanged)
		{
			UpdateTransform(event.flags);
		}
		else if (event.type == EntityEventType::TransformUpdated)
		{
			if (entity->HasFlag(EntityFlags::HasPhysics))
			{
				if (UpdateTransform_Scale & event.flags)
				{
					scene->physicsScene.PhysicsEntityRequireUpdate(entity);
				}
				else
				{
					scene->physicsScene.UpdateTransform(entity);
				}
			}
		}
	}

	void Transform::RegisterType(NativeReflectType<Transform>& type)
	{
		type.Field<&Transform::m_position, &Transform::GetPosition, &Transform::SetPosition>("position");
		type.Field<&Transform::m_rotation, &Transform::GetRotation, &Transform::SetRotation>("rotation");
		type.Field<&Transform::m_scale, &Transform::GetScale, &Transform::SetScale>("scale");
		type.Field<&Transform::m_mobility, &Transform::GetMobility, &Transform::SetMobility>("mobility");

		type.Function<&Transform::SetRotationEuler>("SetRotationEuler", "rotation");
		type.Function<&Transform::SetTransform>("SetTransform", "position", "rotation", "scale");
		type.Function<&Transform::GetLocalTransform>("GetLocalTransform");
		type.Function<&Transform::GetParentWorldTransform>("GetParentWorldTransform");
	}

	void Transform2D::RegisterType(NativeReflectType<Transform2D>& type)
	{
		type.Field<&Transform2D::m_position>("position");
		type.Field<&Transform2D::m_scale>("scale");
		type.Field<&Transform2D::m_rotation>("rotation").Attribute<UISliderProperty>(UISliderProperty{.minValue = -360.0, .maxValue = 360.0});
	}
}