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

		type.Function<&Component::OnCreate>("OnCreate").Attribute<VirtualMethod>();
		type.Function<&Component::OnDestroy>("OnDestroy").Attribute<VirtualMethod>();
		type.Function<&Component::OnStart>("OnStart").Attribute<VirtualMethod>();
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
		if (Tickable* tickable = dynamic_cast<Tickable*>(this))
		{
			entity->m_scene->m_updateToAdd.Enqueue(tickable);
		}

		if (FixedTickable* fixedTickable = dynamic_cast<FixedTickable*>(this))
		{
			entity->m_scene->m_fixedUpdateToAdd.Enqueue(fixedTickable);
		}

		if (dynamic_cast<CollisionListener*>(this))
		{
			entity->m_scene->physicsScene.RegisterCollisionCallbacks(entity);
		}

		if (ReflectType* type = GetType(); type && type->HasAttribute<Iterable>())
		{
			entity->m_scene->m_iterableComponents[type->GetProps().typeId].emplace(this);
		}
	}

	void Component::RemoveEvents()
	{
		if (Tickable* tickable = dynamic_cast<Tickable*>(this))
		{
			entity->m_scene->m_updateToRemove.Enqueue(tickable);
		}

		if (FixedTickable* fixedTickable = dynamic_cast<FixedTickable*>(this))
		{
			entity->m_scene->m_fixedUpdateToRemove.Enqueue(fixedTickable);
		}

		if (dynamic_cast<CollisionListener*>(this))
		{
			// Only unregister if no other component on this entity needs collision callbacks
			bool hasOtherCollisionCallbacks = false;
			for (Component* other : entity->GetComponents())
			{
				if (other != this && dynamic_cast<CollisionListener*>(other))
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

		if (ReflectType* type = GetType(); type && type->HasAttribute<Iterable>())
		{
			if (auto it = entity->m_scene->m_iterableComponents.Find(type->GetProps().typeId))
			{
				it->second.erase(this);
			}
		}
	}

	ComponentProxy::ComponentProxy(ReflectType* type, VoidPtr instance, VoidPtr api) : m_type(type), m_instance(instance), m_api(static_cast<ComponentProxyApi*>(api))
	{
	}

	TypeID ComponentProxy::GetTypeId() const
	{
		if (m_type)
		{
			return m_type->GetProps().typeId;
		}
		return Component::GetTypeId();
	}

	VoidPtr ComponentProxy::GetInstance()
	{
		return m_instance;
	}

	void ComponentProxy::OnCreate()
	{
		if (m_api->onCreate)
		{
			m_api->onCreate(m_instance);
		}
	}

	void ComponentProxy::OnDestroy()
	{
		if (m_api->onDestroy)
		{
			m_api->onDestroy(m_instance);
		}
	}

	void ComponentProxy::OnStart()
	{
		if (m_api->onStart)
		{
			m_api->onStart(m_instance);
		}
	}

	void ComponentProxy::ProcessEvent(const EntityEventDesc& event)
	{
		if (m_api->onProcessEvent)
		{
			m_api->onProcessEvent(m_instance, event);
		}
	}

	void ComponentProxy::RegisterType(NativeReflectType<ComponentProxy>& type)
	{
		type.Constructor<ReflectType*, VoidPtr, VoidPtr>("type", "instance", "api");
	}
}
