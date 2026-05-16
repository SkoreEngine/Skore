#include "Skore/Script/ScriptManager.hpp"

#include "Skore/Core/Reflection.hpp"
#include "Skore/IO/Path.hpp"


namespace Skore
{
	namespace
	{
		HashMap<String, ScriptEngine*> scriptInterfaces;
		Array<ScriptEngine*> scriptEngineList;
	}

	void ScriptEngineInit()
	{
		// Clean up old engines from a previous Init() call (e.g. after App::ResetContext())
		for (auto it = scriptInterfaces.begin(); it != scriptInterfaces.end(); ++it)
		{
			it->second->~ScriptEngine();
			MemFree(it->second);
		}
		scriptInterfaces.Clear();
		scriptEngineList.Clear();

		for (TypeID type : Reflection::GetDerivedTypes(sktypeid(ScriptEngine)))
		{
			if (ReflectType* reflectType = Reflection::FindTypeById(type))
			{
				if (ScriptEngine* interface = reflectType->NewObject()->SafeCast<ScriptEngine>())
				{
					interface->Init();
					scriptInterfaces.Insert(interface->GetExtension(), interface);
					scriptEngineList.EmplaceBack(interface);
				}
			}
		}
	}

	void ScriptManager::Init()
	{
		ScriptEngineInit();
	}

	Span<ScriptEngine*> ScriptManager::GetEngines()
	{
		return Span<ScriptEngine*>(scriptEngineList.Data(), scriptEngineList.Size());
	}

	bool ScriptManager::LoadScript(StringView filePath, StringView extension)
	{
		if (auto it = scriptInterfaces.Find(extension))
		{
			return it->second->LoadScript(filePath);
		}
		return false;
	}

	bool ScriptManager::LoadScript(Span<u8> bytecode, StringView extension)
	{
		if (auto it = scriptInterfaces.Find(extension))
		{
			return it->second->LoadScript(bytecode);
		}
		return false;
	}

	ByteBuffer ScriptManager::BuildScript(StringView source, StringView extension)
	{
		if (auto it = scriptInterfaces.Find(extension))
		{
			return it->second->BuildScript(source);
		}
		return {};
	}
}