#pragma once
#include "Skore/Scene/Component.hpp"


namespace Skore
{
	using EmptyFp = void(*)(VoidPtr instance);
	using EventFp = void(*)(VoidPtr instance, const EntityEventDesc& event);

	struct ScriptComponentApi
	{
		EmptyFp onCreate = {};
		EmptyFp onDestroy = {};
		EmptyFp onStart = {};
		EventFp onProcessEvent = {};
	};

	class ScriptComponent : public Component
	{
	public:

		using Base = Component;

		ScriptComponent(ReflectType* type, VoidPtr instance, ScriptComponentApi* api);

		TypeID GetTypeId() const override;
		void   OnCreate() override;
		void   OnDestroy() override;
		void   OnStart() override;
		void   ProcessEvent(const EntityEventDesc& event) override;

		static void RegisterType(NativeReflectType<ScriptComponent>& type);

	private:
		ReflectType*        m_type{};
		VoidPtr             m_instance{};
		ScriptComponentApi* m_api{};
	};
}
