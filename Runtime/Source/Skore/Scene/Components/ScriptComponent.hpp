#pragma once
#include "Skore/Scene/Component.hpp"


namespace Skore
{
	using EmptyFp = void(*)(VoidPtr self);
	using EventFp = void(*)(VoidPtr self, const EntityEventDesc& event);

	class ScriptComponent : public Component
	{
	public:
		TypeID GetTypeId() const override;
		void   OnCreate() override;
		void   OnDestroy() override;
		void   OnStart() override;
		void   ProcessEvent(const EntityEventDesc& event) override;

	private:
		EmptyFp m_onCreate = {};
		EmptyFp m_onDestroy = {};
		EmptyFp m_onStart = {};
		EventFp m_onProcessEvent = {};
	};
}
