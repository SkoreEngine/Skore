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
}