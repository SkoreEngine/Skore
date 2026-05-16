#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/TypeInfo.hpp"
#include "Skore/Resource/ResourceCommon.hpp"

namespace Skore
{
	// --- Stub Generator (no Lua dependency) ---

	SK_API void GenerateLuaAnnotations(StringView outputPath);

	// --- Script Engine Registration (no Lua dependency) ---

	void RegisterLuaScriptEngine();
}

// The rest of this header requires lua.h. It is only available
// when the consumer has lua in its include path (i.e. Runtime
// and its internal targets).  Editor/Player targets that only need
// GenerateLuaAnnotations() or RegisterLuaScriptEngine() above can
// include this header without lua.
#if __has_include(<lua.h>)

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace Skore
{
	class ReflectField;
	class ReflectType;
	class ReflectFunction;

	struct LuaResourceLoader : ResourceLoader
	{
		SK_CLASS(LuaResourceLoader, ResourceLoader);

		TypeID GetResourceTypeId() override;
		void   LoadResource(RID rid) override;
	};

	// --- Bindings: C++ <-> Lua conversion ---

	bool LuaCppToLua(lua_State* L, const void* cppVal, const FieldProps& props);
	bool LuaLuaToCpp(lua_State* L, int index, const FieldProps& props, void* outBuffer);

	template <typename T>
	inline bool LuaCppToLua(lua_State* L, const T& value)
	{
		return LuaCppToLua(L, &value, GetFieldProps<void, T>());
	}

	template <typename T>
	inline bool LuaLuaToCpp(lua_State* L, int index, T& outValue)
	{
		return LuaLuaToCpp(L, index, GetFieldProps<void, T>(), &outValue);
	}

	// --- Lua Utilities ---

	void InitLuaBindings(lua_State* L);
	lua_State* GetLuaState();

	// --- Lua Type Registry (RegisterComponent) ---

	void InitLuaTypeRegistry(lua_State* L);
	bool IsLuaType(TypeID typeId);
}

#endif // __has_include(<lua.h>)
