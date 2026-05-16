#include "Skore/Script/ScriptManager.hpp"
#include "Skore/Script/LuaScriptEngine.hpp"
#include "Skore/Script/ScriptCommon.hpp"
#include "Skore/Core/Reflection.hpp"

namespace Skore
{
	void RegisterScriptTypes()
	{
		Reflection::Type<ScriptManager>();
		Reflection::Type<ScriptEngine>();

		Resources::Type<LuaScriptResource>()
			.Field<LuaScriptResource::Binary>(ResourceFieldType::Blob)
			.Build();

		Reflection::Type<LuaResourceLoader>();

		RegisterLuaScriptEngine();
	}
}