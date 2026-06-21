#pragma once
#include "Skore/Core/Array.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Object.hpp"
#include "Skore/Core/StringView.hpp"

namespace Skore
{
	class ReflectType;

	class SK_API ScriptEngine : public Object
	{
	public:
		SK_CLASS(ScriptEngine, Object);

		virtual void                Init() = 0;
		virtual void                LoadAssembly(StringView) {}
		virtual Array<ReflectType*> GetScriptTypes() const { return {}; }
	};

	struct SK_API ScriptManager
	{
		static void                Init();
		static void                LoadAssembly(StringView assemblyPath);
		static Array<ReflectType*> GetScriptTypes();
	};
}
