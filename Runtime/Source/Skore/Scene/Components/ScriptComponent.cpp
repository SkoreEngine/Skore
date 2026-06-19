#include "ScriptComponent.hpp"

#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	ScriptComponent::ScriptComponent(ReflectType* type, VoidPtr instance, ScriptComponentApi* api) : m_type(type), m_instance(instance), m_api(api)
	{
	}

	TypeID ScriptComponent::GetTypeId() const
	{
		if (m_type)
		{
			return m_type->GetProps().typeId;
		}
		return Component::GetTypeId();
	}

	void ScriptComponent::OnCreate()
	{
		if (m_api->onCreate)
		{
			m_api->onCreate(m_instance);
		}
	}

	void ScriptComponent::OnDestroy()
	{
		if (m_api->onDestroy)
		{
			m_api->onDestroy(m_instance);
		}
	}

	void ScriptComponent::OnStart()
	{
		if (m_api->onStart)
		{
			m_api->onStart(m_instance);
		}
	}

	void ScriptComponent::ProcessEvent(const EntityEventDesc& event)
	{
		if (m_api->onProcessEvent)
		{
			m_api->onProcessEvent(m_instance, event);
		}
	}

	void ScriptComponent::RegisterType(NativeReflectType<ScriptComponent>& type)
	{
		type.Constructor<ReflectType*, VoidPtr, ScriptComponentApi*>("type", "instance", "api");
	}
}
