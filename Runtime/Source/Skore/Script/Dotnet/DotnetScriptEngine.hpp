#pragma once

#include "Skore/Script/ScriptManager.hpp"

namespace Skore
{
	class SK_API DotnetScriptEngine : public ScriptEngine
	{
	public:
		SK_CLASS(DotnetScriptEngine, ScriptEngine);

		void Init() override;

	private:
		VoidPtr m_hostfxrLibrary = nullptr;
		VoidPtr m_hostContext = nullptr;
		VoidPtr m_loadAssemblyAndGetFunctionPointer = nullptr;
	};
}
