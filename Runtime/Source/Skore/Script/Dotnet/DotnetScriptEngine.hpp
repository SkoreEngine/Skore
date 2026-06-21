#pragma once

#include "Skore/Script/ScriptManager.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/TypeInfo.hpp"

namespace Skore
{
	class ReflectType;

	class SK_API DotnetScriptEngine : public ScriptEngine
	{
	public:
		SK_CLASS(DotnetScriptEngine, ScriptEngine);

		void Init() override;

		void LoadAssembly(StringView assemblyPath);

		Array<ReflectType*> GetScriptTypes() const;

	private:
		using FnLoadAssemblyCallback = void (*)(VoidPtr userData, TypeID typeId);
		using FnLoadAssembly = void (*)(StringView assemblyPath, VoidPtr userData, FnLoadAssemblyCallback callback);

		static void LoadAssemblyCallback(VoidPtr userData, TypeID typeId);

		VoidPtr m_hostfxrLibrary = nullptr;
		VoidPtr m_hostContext = nullptr;
		VoidPtr m_loadAssemblyAndGetFunctionPointer = nullptr;

		FnLoadAssembly m_fnLoadAssembly = nullptr;

		HashMap<String, Array<ReflectType*>> m_scriptTypes{};
	};
}
