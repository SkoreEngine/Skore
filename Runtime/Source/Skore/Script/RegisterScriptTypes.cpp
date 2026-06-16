#include "Skore/Script/ScriptManager.hpp"
#include "Skore/Script/Dotnet/DotnetScriptEngine.hpp"
#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	void RegisterScriptTypes()
	{
		Reflection::Type<ScriptManager>();
		Reflection::Type<ScriptEngine>();
		Reflection::Type<DotnetScriptEngine>();
	}
}