#include "Skore/Scene/Component.hpp"

#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/Reflection.hpp"


namespace Skore
{
	void Component::RegisterType(NativeReflectType<Component>& type)
	{
		type.Function<&Component::GetEntity>("GetEntity");
		type.Function<&Component::GetScene>("GetScene");
		type.Function<&Component::GetRID>("GetRID");

		type.Function<&Component::Create>("Create", "settings").Attribute<VirtualMethod>();
		type.Function<&Component::Destroy>("Destroy").Attribute<VirtualMethod>();
		type.Function<&Component::OnStart>("OnStart").Attribute<VirtualMethod>();
		type.Function<&Component::OnUpdate>("OnUpdate", "deltaTime").Attribute<VirtualMethod>();
		type.Function<&Component::OnFixedUpdate>("OnFixedUpdate").Attribute<VirtualMethod>();
		type.Function<&Component::OnCollisionEnter>("OnCollisionEnter", "collision").Attribute<VirtualMethod>();
		type.Function<&Component::OnCollisionStay>("OnCollisionStay", "collision").Attribute<VirtualMethod>();
		type.Function<&Component::OnCollisionExit>("OnCollisionExit", "collision").Attribute<VirtualMethod>();
		type.Function<&Component::OnTriggerEnter>("OnTriggerEnter", "collision").Attribute<VirtualMethod>();
		type.Function<&Component::OnTriggerExit>("OnTriggerExit", "collision").Attribute<VirtualMethod>();
		type.Function<&Component::ProcessEvent>("ProcessEvent", "event").Attribute<VirtualMethod>();
	}

	RID Component::GetRID() const
	{
		return m_rid;
	}

	void Component::PhysicsRequireUpdate() const
	{
		scene->physicsScene.PhysicsEntityRequireUpdate(entity);
	}

	void Component::RegisterEvents()
	{
		if (m_settings.enableUpdate)
		{
			entity->m_scene->m_updateToAdd.Enqueue(this);
		}

		if (m_settings.enableFixedUpdate)
		{
			entity->m_scene->m_fixedUpdateComponents.emplace(this);
		}

		if (m_settings.enableCollisionCallbacks)
		{
			entity->m_scene->physicsScene.RegisterCollisionCallbacks(entity);
		}
	}

	void Component::RemoveEvents()
	{
		if (m_settings.enableUpdate)
		{
			entity->m_scene->m_updateToRemove.Enqueue(this);
		}

		if (m_settings.enableFixedUpdate)
		{
			entity->m_scene->m_fixedUpdateComponents.erase(this);
		}

		if (m_settings.enableCollisionCallbacks)
		{
			// Only unregister if no other component on this entity needs collision callbacks
			bool hasOtherCollisionCallbacks = false;
			for (Component* other : entity->GetComponents())
			{
				if (other != this && other->m_settings.enableCollisionCallbacks)
				{
					hasOtherCollisionCallbacks = true;
					break;
				}
			}
			if (!hasOtherCollisionCallbacks)
			{
				entity->m_scene->physicsScene.UnregisterCollisionCallbacks(entity);
			}
		}
	}
}