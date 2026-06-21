#include "Skore/Script/ScriptManager.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/IO/Path.hpp"


namespace Skore
{
	namespace
	{
		Array<ScriptEngine*> scriptEngineList;
	}

	void ScriptEngineInit()
	{
		scriptEngineList.Clear();

		for (TypeID type : Reflection::GetDerivedTypes(sktypeid(ScriptEngine)))
		{
			if (ReflectType* reflectType = Reflection::FindTypeById(type))
			{
				if (ScriptEngine* interface = reflectType->NewObject()->SafeCast<ScriptEngine>())
				{
					interface->Init();
					//scriptInterfaces.Insert(interface->GetExtension(), interface);
					scriptEngineList.EmplaceBack(interface);
				}
			}
		}
	}

	void ScriptManager::Init()
	{
		ScriptEngineInit();
	}

	void ScriptManager::LoadAssembly(StringView assemblyPath)
	{
		for (ScriptEngine* engine : scriptEngineList)
		{
			engine->LoadAssembly(assemblyPath);
		}
	}

	Array<ReflectType*> ScriptManager::GetScriptTypes()
	{
		Array<ReflectType*> result;
		for (ScriptEngine* engine : scriptEngineList)
		{
			for (ReflectType* type : engine->GetScriptTypes())
			{
				result.EmplaceBack(type);
			}
		}
		return result;
	}
}