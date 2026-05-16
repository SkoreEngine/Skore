#include "Skore/Script/ScriptManager.hpp"
#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	void RegisterScriptTypes()
	{
		Reflection::Type<ScriptManager>();
		Reflection::Type<ScriptEngine>();
	}
}