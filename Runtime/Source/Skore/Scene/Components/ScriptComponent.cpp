#include "ScriptComponent.hpp"

namespace Skore
{
	TypeID ScriptComponent::GetTypeId() const
	{
		return Component::GetTypeId();
	}

	void ScriptComponent::OnCreate()
	{
		if (m_onCreate)
		{
			m_onCreate(this);
		}
	}

	void ScriptComponent::OnDestroy()
	{
		if (m_onDestroy)
		{
			m_onDestroy(this);
		}
	}

	void ScriptComponent::OnStart()
	{
		if (m_onStart)
		{
			m_onStart(this);
		}
	}

	void ScriptComponent::ProcessEvent(const EntityEventDesc& event)
	{
		if (m_onProcessEvent)
		{
			m_onProcessEvent(this, event);
		}
	}
}
