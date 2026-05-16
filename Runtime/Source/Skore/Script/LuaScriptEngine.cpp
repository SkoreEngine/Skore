#include "Skore/Script/LuaScriptEngine.hpp"
#include "Skore/Script/ScriptManager.hpp"
#include "Skore/Script/Wrappers/LuaComponentWrapper.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Attributes.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Hash.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Math.hpp"
#include "Skore/Core/Color.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceObject.hpp"
#include "Skore/App.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Entity.hpp"

#include <string>

#include "Skore/Script/ScriptCommon.hpp"

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

// ============================================================================
// Lua Bindings implementation
// ============================================================================

namespace Skore
{
	namespace
	{
		Logger& luaBindingsLogger = Logger::GetLogger("Skore::LuaBindings");

		// --- Per-type binding cache ---
		// Heap-allocated so pointers remain stable across HashMap rehashes.

		struct LuaTypeBindingCache
		{
			HashMap<String, ReflectField*>           fieldsByName;
			HashMap<String, Array<ReflectFunction*>> functionsByName;
		};

		HashMap<String, LuaTypeBindingCache*> typeBindingCaches;    // metatableName -> cache*
		HashMap<TypeID, String>               typeIdToMetatableName;

		// --- Userdata struct ---

		struct LuaSkoreObject
		{
			void*        instance;
			ReflectType* type;
			bool         ownsMemory;
		};

		constexpr usize kMaxStackParams = 8;
		constexpr usize kMaxStackParamStorage = 128;
		constexpr usize kMaxStackReturnStorage = 64;
		constexpr usize kFieldStackBufferSize = 64;

		// Forward declarations
		void DestroyConvertedValue(void* buffer, const FieldProps& props);

		// ----------------------------------------------------------------
		// C++ -> Lua   (push onto Lua stack, returns true on success)
		// ----------------------------------------------------------------

		bool CppToLua(lua_State* L, const void* cppVal, const FieldProps& props)
		{
			TypeID typeId = props.typeId;

			// Enum
			if (props.isEnum)
			{
				lua_Integer intVal = 0;
				switch (props.size)
				{
					case 1: intVal = *static_cast<const i8*>(cppVal); break;
					case 2: intVal = *static_cast<const i16*>(cppVal); break;
					case 4: intVal = *static_cast<const i32*>(cppVal); break;
					case 8: intVal = *static_cast<const i64*>(cppVal); break;
					default: lua_pushnil(L); return false;
				}
				lua_pushinteger(L, intVal);
				return true;
			}

			// Primitives
			if (typeId == TypeInfo<i32>::ID())
			{
				lua_pushinteger(L, *static_cast<const i32*>(cppVal));
				return true;
			}
			if (typeId == TypeInfo<i64>::ID())
			{
				lua_pushinteger(L, static_cast<lua_Integer>(*static_cast<const i64*>(cppVal)));
				return true;
			}
			if (typeId == TypeInfo<u32>::ID())
			{
				lua_pushinteger(L, *static_cast<const u32*>(cppVal));
				return true;
			}
			if (typeId == TypeInfo<u64>::ID())
			{
				lua_pushinteger(L, static_cast<lua_Integer>(*static_cast<const u64*>(cppVal)));
				return true;
			}
			if (typeId == TypeInfo<TypeID>::ID())
			{
				lua_pushinteger(L, static_cast<lua_Integer>(static_cast<const TypeID*>(cppVal)->id));
				return true;
			}
			if (typeId == TypeInfo<f32>::ID())
			{
				lua_pushnumber(L, static_cast<lua_Number>(*static_cast<const f32*>(cppVal)));
				return true;
			}
			if (typeId == TypeInfo<f64>::ID())
			{
				lua_pushnumber(L, static_cast<lua_Number>(*static_cast<const f64*>(cppVal)));
				return true;
			}
			if (typeId == TypeInfo<bool>::ID())
			{
				lua_pushboolean(L, *static_cast<const bool*>(cppVal));
				return true;
			}
			if (typeId == TypeInfo<String>::ID())
			{
				const String* s = static_cast<const String*>(cppVal);
				lua_pushlstring(L, s->CStr(), s->Size());
				return true;
			}
			if (typeId == TypeInfo<StringView>::ID())
			{
				StringView sv = *static_cast<const StringView*>(cppVal);
				lua_pushlstring(L, sv.Data(), sv.Size());
				return true;
			}

			// Array<T> -> Lua table (sequence)
			if (props.typeApi == TypeInfo<ArrayApi>::ID() && props.getTypeApi)
			{
				ArrayApi api{};
				props.getTypeApi(&api);

				const void* arrayPtr = props.isPointer ? *static_cast<void* const*>(cppVal) : cppVal;
				if (!arrayPtr)
				{
					lua_pushnil(L);
					return true;
				}

				usize size = api.Size(arrayPtr);
				TypeProps elemTypeProps = api.GetProps();
				FieldProps elemFieldProps = ToFieldProps(elemTypeProps);

				lua_createtable(L, static_cast<int>(size), 0);
				for (usize i = 0; i < size; i++)
				{
					VoidPtr elemPtr = api.Get(const_cast<VoidPtr>(arrayPtr), i);
					CppToLua(L, elemPtr, elemFieldProps);
					lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1)); // Lua is 1-indexed
				}
				return true;
			}

			// Span<T> -> Lua table (sequence)
			if (props.typeApi == TypeInfo<SpanApi>::ID() && props.getTypeApi)
			{
				SpanApi api{};
				props.getTypeApi(&api);

				const void* spanPtr = props.isPointer ? *static_cast<void* const*>(cppVal) : cppVal;
				if (!spanPtr)
				{
					lua_pushnil(L);
					return true;
				}

				usize size = api.Size(spanPtr);
				TypeProps elemTypeProps = api.GetProps();
				FieldProps elemFieldProps = ToFieldProps(elemTypeProps);

				lua_createtable(L, static_cast<int>(size), 0);
				for (usize i = 0; i < size; i++)
				{
					VoidPtr elemPtr = api.Get(const_cast<VoidPtr>(spanPtr), i);
					CppToLua(L, elemPtr, elemFieldProps);
					lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
				}
				return true;
			}

			// HashMap<K,V> -> Lua table (dict)
			if (props.typeApi == TypeInfo<HashMapApi>::ID() && props.getTypeApi)
			{
				HashMapApi api{};
				props.getTypeApi(&api);

				const void* mapPtr = props.isPointer ? *static_cast<void* const*>(cppVal) : cppVal;
				if (!mapPtr)
				{
					lua_pushnil(L);
					return true;
				}

				TypeProps keyTypeProps = api.GetKeyProps();
				TypeProps valTypeProps = api.GetValueProps();
				FieldProps keyFieldProps = ToFieldProps(keyTypeProps);
				FieldProps valFieldProps = ToFieldProps(valTypeProps);

				lua_newtable(L);

				struct ForEachCtx
				{
					lua_State* L;
					FieldProps keyProps;
					FieldProps valProps;
				};

				ForEachCtx ctx{L, keyFieldProps, valFieldProps};

				api.ForEach(mapPtr, [](ConstPtr key, ConstPtr value, VoidPtr ud) -> bool
				{
					ForEachCtx* c = static_cast<ForEachCtx*>(ud);
					CppToLua(c->L, key, c->keyProps);
					CppToLua(c->L, value, c->valProps);
					lua_settable(c->L, -3);
					return true;
				}, &ctx);

				return true;
			}

			// Reflected userdata types
			auto metaIt = typeIdToMetatableName.Find(typeId);
			if (metaIt)
			{
				const String& metaName = metaIt->second;

				if (props.isPointer || props.isReference)
				{
					void* ptr = *static_cast<void* const*>(cppVal);
					if (!ptr)
					{
						lua_pushnil(L);
						return true;
					}

					// If the pointer is to a Component (or derived), the actual
					// runtime object may be a Lua LuaComponentWrapper.  In that
					// case return the stored Lua self-table directly so the
					// caller can access Lua-defined fields.
					// For C++ components, resolve the actual runtime type so that
					// e.g. GetComponent(TypeID)->Component* is wrapped with the
					// correct metatable, making subclass fields accessible.
					ReflectType* resolvedType = Reflection::FindTypeById(typeId);
					const String* resolvedMetaName = &metaName;

					if (resolvedType &&
					    (typeId == TypeInfo<Component>::ID() || resolvedType->IsDerivedOf(TypeInfo<Component>::ID())))
					{
						Component* comp = static_cast<Component*>(ptr);
						TypeID actualTypeId = comp->GetTypeId();

						if (IsLuaType(actualTypeId))
						{
							LuaComponentWrapper* cw = static_cast<LuaComponentWrapper*>(comp);
							int selfRef = cw->GetSelfRef();
							if (selfRef != LUA_NOREF)
							{
								lua_rawgeti(L, LUA_REGISTRYINDEX, selfRef);
								return true;
							}
						}
						else if (actualTypeId != typeId)
						{
							// Resolve actual C++ runtime type (e.g. Transform instead of Component)
							auto actualMetaIt = typeIdToMetatableName.Find(actualTypeId);
							if (actualMetaIt)
							{
								ReflectType* actualType = Reflection::FindTypeById(actualTypeId);
								if (actualType)
								{
									resolvedType = actualType;
									resolvedMetaName = &actualMetaIt->second;
								}
							}
						}
					}

					LuaSkoreObject* ud = static_cast<LuaSkoreObject*>(lua_newuserdatauv(L, sizeof(LuaSkoreObject), 0));
					ud->instance = ptr;
					ud->type = resolvedType;
					ud->ownsMemory = false;
					luaL_setmetatable(L, resolvedMetaName->CStr());
				}
				else
				{
					ReflectType* reflType = Reflection::FindTypeById(typeId);
					if (!reflType)
					{
						lua_pushnil(L);
						return false;
					}
					LuaSkoreObject* ud = static_cast<LuaSkoreObject*>(lua_newuserdatauv(L, sizeof(LuaSkoreObject), 0));
					ud->type = reflType;
					ud->ownsMemory = true;
					ud->instance = MemAlloc(props.size);
					reflType->Copy(cppVal, ud->instance);
					luaL_setmetatable(L, metaName.CStr());
				}
				return true;
			}

			lua_pushnil(L);
			return false;
		}

		// ----------------------------------------------------------------
		// Lua -> C++   (read from Lua stack at `index`)
		// ----------------------------------------------------------------

		bool LuaToCpp(lua_State* L, int index, const FieldProps& props, void* outBuffer)
		{
			TypeID typeId = props.typeId;

			// Enum
			if (props.isEnum)
			{
				if (lua_isinteger(L, index))
				{
					lua_Integer intVal = lua_tointeger(L, index);
					switch (props.size)
					{
						case 1: *static_cast<i8*>(outBuffer) = static_cast<i8>(intVal); break;
						case 2: *static_cast<i16*>(outBuffer) = static_cast<i16>(intVal); break;
						case 4: *static_cast<i32*>(outBuffer) = static_cast<i32>(intVal); break;
						case 8: *static_cast<i64*>(outBuffer) = static_cast<i64>(intVal); break;
						default: return false;
					}
					return true;
				}
				return false;
			}

			// Primitives
			if (typeId == TypeInfo<i32>::ID())
			{
				if (lua_isinteger(L, index))
				{
					*static_cast<i32*>(outBuffer) = static_cast<i32>(lua_tointeger(L, index));
					return true;
				}
				return false;
			}
			if (typeId == TypeInfo<i64>::ID())
			{
				if (lua_isinteger(L, index))
				{
					*static_cast<i64*>(outBuffer) = static_cast<i64>(lua_tointeger(L, index));
					return true;
				}
				return false;
			}
			if (typeId == TypeInfo<u32>::ID())
			{
				if (lua_isinteger(L, index))
				{
					*static_cast<u32*>(outBuffer) = static_cast<u32>(lua_tointeger(L, index));
					return true;
				}
				return false;
			}
			if (typeId == TypeInfo<u64>::ID())
			{
				if (lua_isinteger(L, index))
				{
					*static_cast<u64*>(outBuffer) = static_cast<u64>(lua_tointeger(L, index));
					return true;
				}
				return false;
			}
			if (typeId == TypeInfo<TypeID>::ID())
			{
				if (lua_isinteger(L, index))
				{
					TypeID tid;
					tid.id = static_cast<u64>(lua_tointeger(L, index));
					*static_cast<TypeID*>(outBuffer) = tid;
					return true;
				}
				// Accept a type table with __typeId field (C++ bound types)
				if (lua_istable(L, index))
				{
					lua_getfield(L, index, "__typeId");
					if (lua_isinteger(L, -1))
					{
						TypeID tid;
						tid.id = static_cast<u64>(lua_tointeger(L, -1));
						lua_pop(L, 1);
						*static_cast<TypeID*>(outBuffer) = tid;
						return true;
					}
					lua_pop(L, 1);
				}
				return false;
			}
			if (typeId == TypeInfo<f32>::ID())
			{
				if (lua_isnumber(L, index))
				{
					*static_cast<f32*>(outBuffer) = static_cast<f32>(lua_tonumber(L, index));
					return true;
				}
				return false;
			}
			if (typeId == TypeInfo<f64>::ID())
			{
				if (lua_isnumber(L, index))
				{
					*static_cast<f64*>(outBuffer) = static_cast<f64>(lua_tonumber(L, index));
					return true;
				}
				return false;
			}
			if (typeId == TypeInfo<bool>::ID())
			{
				if (lua_isboolean(L, index))
				{
					*static_cast<bool*>(outBuffer) = lua_toboolean(L, index) != 0;
					return true;
				}
				return false;
			}
			if (typeId == TypeInfo<String>::ID())
			{
				if (lua_isstring(L, index))
				{
					size_t len = 0;
					const char* str = lua_tolstring(L, index, &len);
					new (outBuffer) String(str, len);
					return true;
				}
				return false;
			}
			if (typeId == TypeInfo<StringView>::ID())
			{
				if (lua_isstring(L, index))
				{
					const char* str = lua_tostring(L, index);
					new (outBuffer) StringView(str);
					return true;
				}
				return false;
			}

			// Lua table -> Array<T>
			if (props.typeApi == TypeInfo<ArrayApi>::ID() && props.getTypeApi && lua_istable(L, index))
			{
				ArrayApi api{};
				props.getTypeApi(&api);

				TypeProps elemTypeProps = api.GetProps();
				FieldProps elemFieldProps = ToFieldProps(elemTypeProps);

				VoidPtr tempArr = api.Create();

				lua_Integer listLen = lua_rawlen(L, index);
				for (lua_Integer i = 1; i <= listLen; i++) // Lua is 1-indexed
				{
					lua_rawgeti(L, index, i);
					VoidPtr slot = api.PushNew(tempArr);
					LuaToCpp(L, -1, elemFieldProps, slot);
					lua_pop(L, 1);
				}

				props.fnCopy(outBuffer, tempArr);
				api.Destroy(tempArr);
				return true;
			}

			// Lua table -> HashMap<K,V>
			if (props.typeApi == TypeInfo<HashMapApi>::ID() && props.getTypeApi && lua_istable(L, index))
			{
				HashMapApi api{};
				props.getTypeApi(&api);

				TypeProps keyTypeProps = api.GetKeyProps();
				TypeProps valTypeProps = api.GetValueProps();
				FieldProps keyFieldProps = ToFieldProps(keyTypeProps);
				FieldProps valFieldProps = ToFieldProps(valTypeProps);

				VoidPtr tempMap = api.Create();

				alignas(16) u8 keyBuf[128];
				alignas(16) u8 valBuf[128];

				lua_pushnil(L); // first key
				while (lua_next(L, index) != 0)
				{
					// key at -2, value at -1
					memset(keyBuf, 0, sizeof(keyBuf));
					memset(valBuf, 0, sizeof(valBuf));
					LuaToCpp(L, -2, keyFieldProps, keyBuf);
					LuaToCpp(L, -1, valFieldProps, valBuf);
					api.Insert(tempMap, keyBuf, valBuf);
					DestroyConvertedValue(keyBuf, keyFieldProps);
					DestroyConvertedValue(valBuf, valFieldProps);
					lua_pop(L, 1); // pop value, keep key for next iteration
				}

				props.fnCopy(outBuffer, tempMap);
				api.Destroy(tempMap);
				return true;
			}

			// Reflected userdata
			if (lua_isuserdata(L, index))
			{
				LuaSkoreObject* wrapper = static_cast<LuaSkoreObject*>(lua_touserdata(L, index));
				if (wrapper && wrapper->type && wrapper->type->GetProps().typeId == typeId)
				{
					if (props.isPointer)
					{
						*static_cast<void**>(outBuffer) = wrapper->instance;
					}
					else
					{
						memcpy(outBuffer, wrapper->instance, props.size);
					}
					return true;
				}
			}

			return false;
		}

		// ----------------------------------------------------------------
		// Destroy non-trivial converted values (String)
		// ----------------------------------------------------------------

		void DestroyConvertedValue(void* buffer, const FieldProps& props)
		{
			if (props.typeId == TypeInfo<String>::ID())
			{
				static_cast<String*>(buffer)->~String();
			}
			else if ((props.typeApi == TypeInfo<ArrayApi>::ID() || props.typeApi == TypeInfo<HashMapApi>::ID()) && props.fnDestroy)
			{
				props.fnDestroy(buffer);
			}
		}

		void DestroyConvertedParams(void** paramPointers, Span<ReflectParam*> params, int count)
		{
			for (int i = 0; i < count; i++)
			{
				DestroyConvertedValue(paramPointers[i], params[i]->GetProps());
			}
		}

		// ----------------------------------------------------------------
		// Reflect function invocation helper
		// ----------------------------------------------------------------

		bool InvokeReflectFunction(lua_State* L, ReflectFunction* func, void* instance, int argStart, int argCount)
		{
			Span<ReflectParam*> params = func->GetParams();
			int expectedParams = static_cast<int>(params.Size());

			if (argCount != expectedParams)
			{
				luaBindingsLogger.Error("Expected {} arguments, got {}", expectedParams, argCount);
				return false;
			}

			alignas(16) u8 stackParamStorage[kMaxStackParamStorage];
			void*          stackParamPointers[kMaxStackParams];

			Array<u8>    heapParamStorage;
			Array<void*> heapParamPointers;

			u8*    paramStoragePtr = stackParamStorage;
			void** paramPointersPtr = stackParamPointers;
			usize  currentOffset = 0;

			usize totalStorageNeeded = 0;
			for (int i = 0; i < expectedParams; i++)
			{
				const FieldProps& fprops = params[i]->GetProps();
				usize alignment = fprops.alignment > 0 ? fprops.alignment : 1;
				totalStorageNeeded = (totalStorageNeeded + alignment - 1) & ~(alignment - 1);
				totalStorageNeeded += fprops.size;
			}

			bool useHeap = (expectedParams > static_cast<int>(kMaxStackParams)) ||
			               (totalStorageNeeded > kMaxStackParamStorage);
			if (useHeap)
			{
				heapParamStorage.Resize(totalStorageNeeded);
				heapParamPointers.Reserve(expectedParams);
				paramStoragePtr = heapParamStorage.Data();
			}

			int convertedCount = 0;
			for (int i = 0; i < expectedParams; i++)
			{
				const FieldProps& fprops = params[i]->GetProps();
				usize alignment = fprops.alignment > 0 ? fprops.alignment : 1;
				usize alignedOffset = (currentOffset + alignment - 1) & ~(alignment - 1);

				void* paramPtr = paramStoragePtr + alignedOffset;

				if (useHeap)
				{
					heapParamPointers.EmplaceBack(paramPtr);
				}
				else
				{
					paramPointersPtr[i] = paramPtr;
				}

				if (!LuaToCpp(L, argStart + i, fprops, paramPtr))
				{
					DestroyConvertedParams(useHeap ? heapParamPointers.Data() : paramPointersPtr, params, convertedCount);
					luaBindingsLogger.Error("Cannot convert argument {}", i);
					return false;
				}

				convertedCount++;
				currentOffset = alignedOffset + fprops.size;
			}

			void** finalParamPointers = useHeap ? heapParamPointers.Data() : paramPointersPtr;

			FieldProps returnProps = func->GetReturn();

			alignas(16) u8 stackReturnStorage[kMaxStackReturnStorage];
			Array<u8>      heapReturnStorage;
			void*          returnPtr;

			if (returnProps.size <= kMaxStackReturnStorage)
			{
				returnPtr = stackReturnStorage;
			}
			else
			{
				heapReturnStorage.Resize(returnProps.size);
				returnPtr = heapReturnStorage.Data();
			}

			memset(returnPtr, 0, returnProps.size);
			func->Invoke(instance, returnPtr, finalParamPointers);

			DestroyConvertedParams(finalParamPointers, params, expectedParams);

			static TypeID voidTypeId = TypeInfo<void>::ID();

			if (returnProps.typeId.id != 0 && returnProps.typeId != voidTypeId)
			{
				CppToLua(L, returnPtr, returnProps);
				DestroyConvertedValue(returnPtr, returnProps);
				return true;  // 1 return value on stack
			}

			DestroyConvertedValue(returnPtr, returnProps);
			return false;  // 0 return values
		}

		// ----------------------------------------------------------------
		// Metamethods
		// ----------------------------------------------------------------

		// __gc metamethod
		int LuaSkoreObject_gc(lua_State* L)
		{
			LuaSkoreObject* obj = static_cast<LuaSkoreObject*>(lua_touserdata(L, 1));
			if (obj && obj->ownsMemory && obj->instance && obj->type)
			{
				obj->type->Destroy(obj->instance);
				obj->instance = nullptr;
			}
			return 0;
		}

		// __tostring metamethod
		int LuaSkoreObject_tostring(lua_State* L)
		{
			LuaSkoreObject* obj = static_cast<LuaSkoreObject*>(lua_touserdata(L, 1));
			if (obj && obj->type)
			{
				lua_pushfstring(L, "%s(%p)", obj->type->GetSimpleName().CStr(), obj->instance);
			}
			else
			{
				lua_pushstring(L, "SkoreObject(null)");
			}
			return 1;
		}

		// __index metamethod: upvalue 1 = cache pointer
		int LuaSkoreObject_index(lua_State* L)
		{
			LuaSkoreObject* wrapper = static_cast<LuaSkoreObject*>(lua_touserdata(L, 1));
			if (!wrapper || !wrapper->instance || !wrapper->type)
			{
				lua_pushnil(L);
				return 1;
			}

			const char* key = lua_tostring(L, 2);
			if (!key)
			{
				lua_pushnil(L);
				return 1;
			}

			LuaTypeBindingCache* cache = static_cast<LuaTypeBindingCache*>(lua_touserdata(L, lua_upvalueindex(1)));
			StringView keyView(key);

			// Check fields
			auto fieldIt = cache->fieldsByName.Find(keyView);
			if (fieldIt)
			{
				ReflectField* field = fieldIt->second;
				const FieldProps& fprops = field->GetProps();

				alignas(16) u8 stackBuffer[kFieldStackBufferSize];
				Array<u8>      heapBuffer;
				u8*            valuePtr;

				if (fprops.size <= kFieldStackBufferSize)
				{
					valuePtr = stackBuffer;
				}
				else
				{
					heapBuffer.Resize(fprops.size);
					valuePtr = heapBuffer.Data();
				}

				field->Get(wrapper->instance, valuePtr, fprops.size);
				CppToLua(L, valuePtr, fprops);
				DestroyConvertedValue(valuePtr, fprops);
				return 1;
			}

			// Check methods - push a closure that captures instance info
			auto funcIt = cache->functionsByName.Find(keyView);
			if (funcIt)
			{
				Array<ReflectFunction*>& funcs = funcIt->second;

				// Check if all overloads are static
				bool allStatic = true;
				for (ReflectFunction* f : funcs)
				{
					if (!f->IsStatic())
					{
						allStatic = false;
						break;
					}
				}

				if (allStatic)
				{
					// Static methods shouldn't be called with instance - push static closure
					lua_pushlightuserdata(L, &funcs);
					lua_pushcclosure(L, [](lua_State* L) -> int
					{
						Array<ReflectFunction*>* funcArr = static_cast<Array<ReflectFunction*>*>(lua_touserdata(L, lua_upvalueindex(1)));
						int argc = lua_gettop(L);

						ReflectFunction* func = nullptr;
						for (ReflectFunction* f : *funcArr)
						{
							if (f->IsStatic() && static_cast<int>(f->GetParams().Size()) == argc)
							{
								func = f;
								break;
							}
						}

						if (!func && !funcArr->Empty())
						{
							for (ReflectFunction* f : *funcArr)
							{
								if (f->IsStatic())
								{
									func = f;
									break;
								}
							}
						}

						if (!func)
						{
							luaBindingsLogger.Error("No matching static method overload");
							return 0;
						}

						bool hasReturn = InvokeReflectFunction(L, func, nullptr, 1, argc);
						return hasReturn ? 1 : 0;
					}, 1);
				}
				else
				{
					// Instance methods - capture userdata pointer and function array
					lua_pushlightuserdata(L, wrapper);
					lua_pushlightuserdata(L, &funcs);
					lua_pushcclosure(L, [](lua_State* L) -> int
					{
						LuaSkoreObject* inst = static_cast<LuaSkoreObject*>(lua_touserdata(L, lua_upvalueindex(1)));
						Array<ReflectFunction*>* funcArr = static_cast<Array<ReflectFunction*>*>(lua_touserdata(L, lua_upvalueindex(2)));

						// With colon syntax: first arg is self (the userdata), actual args start at 2
						// But since we captured instance in upvalue, we use all stack args
						int argc = lua_gettop(L);

						// The first argument might be self from colon syntax - skip it if it's userdata
						int argStart = 1;
						int argCount = argc;
						if (argc > 0 && lua_isuserdata(L, 1))
						{
							argStart = 2;
							argCount = argc - 1;
						}

						ReflectFunction* func = nullptr;
						for (ReflectFunction* f : *funcArr)
						{
							if (!f->IsStatic() && static_cast<int>(f->GetParams().Size()) == argCount)
							{
								func = f;
								break;
							}
						}

						if (!func)
						{
							// Try static overloads
							for (ReflectFunction* f : *funcArr)
							{
								if (f->IsStatic() && static_cast<int>(f->GetParams().Size()) == argCount)
								{
									func = f;
									break;
								}
							}
						}

						if (!func && !funcArr->Empty())
						{
							func = (*funcArr)[0];
							argCount = static_cast<int>(func->GetParams().Size());
						}

						if (!func)
						{
							luaBindingsLogger.Error("No matching method overload");
							return 0;
						}

						void* instance = func->IsStatic() ? nullptr : inst->instance;
						bool hasReturn = InvokeReflectFunction(L, func, instance, argStart, argCount);
						return hasReturn ? 1 : 0;
					}, 2);
				}
				return 1;
			}

			// Not found - check metatable itself for metamethods
			if (luaL_getmetafield(L, 1, key))
			{
				return 1;
			}

			luaBindingsLogger.Error("'{}' has no field or method '{}'", wrapper->type->GetSimpleName(), key);
			lua_pushnil(L);
			return 1;
		}

		// __newindex metamethod: upvalue 1 = cache pointer
		int LuaSkoreObject_newindex(lua_State* L)
		{
			LuaSkoreObject* wrapper = static_cast<LuaSkoreObject*>(lua_touserdata(L, 1));
			if (!wrapper || !wrapper->instance || !wrapper->type)
			{
				return luaL_error(L, "Invalid object in __newindex");
			}

			const char* key = lua_tostring(L, 2);
			if (!key)
			{
				return luaL_error(L, "Invalid key in __newindex");
			}

			LuaTypeBindingCache* cache = static_cast<LuaTypeBindingCache*>(lua_touserdata(L, lua_upvalueindex(1)));
			if (!cache)
			{
				return luaL_error(L, "__newindex: null cache pointer");
			}

			StringView keyView(key);
			auto fieldIt = cache->fieldsByName.Find(keyView);
			if (fieldIt)
			{
				ReflectField* field = fieldIt->second;
				const FieldProps& fprops = field->GetProps();

				alignas(16) u8 stackBuffer[kFieldStackBufferSize];
				Array<u8>      heapBuffer;
				u8*            valuePtr;

				if (fprops.size <= kFieldStackBufferSize)
				{
					valuePtr = stackBuffer;
				}
				else
				{
					heapBuffer.Resize(fprops.size);
					valuePtr = heapBuffer.Data();
				}

				if (!LuaToCpp(L, 3, fprops, valuePtr))
				{
					return luaL_error(L, "Cannot convert value for field '%s'", key);
				}

				field->Set(wrapper->instance, valuePtr, fprops.size);
				DestroyConvertedValue(valuePtr, fprops);
				return 0;
			}

			return luaL_error(L, "No writable field '%s' on type '%s'", key, wrapper->type->GetSimpleName().CStr());
		}

		// ----------------------------------------------------------------
		// Constructor binding
		// ----------------------------------------------------------------

		int LuaSkoreObject_new(lua_State* L)
		{
			// Upvalue 1 = ReflectType*
			ReflectType* reflType = static_cast<ReflectType*>(lua_touserdata(L, lua_upvalueindex(1)));
			if (!reflType)
			{
				return luaL_error(L, "Invalid type in constructor");
			}

			int argc = lua_gettop(L);

			ReflectConstructor* ctor = nullptr;

			if (argc == 0)
			{
				ctor = reflType->GetDefaultConstructor();
			}
			else
			{
				for (ReflectConstructor* c : reflType->GetConstructors())
				{
					if (c->GetParams().Size() == static_cast<usize>(argc))
					{
						ctor = c;
						break;
					}
				}
			}

			if (!ctor)
			{
				ctor = reflType->GetDefaultConstructor();
			}

			if (!ctor)
			{
				return luaL_error(L, "No suitable constructor found for %s", reflType->GetSimpleName().CStr());
			}

			const TypeProps& tprops = reflType->GetProps();
			void* memory = MemAlloc(tprops.size);

			Span<ReflectParam*> params = ctor->GetParams();
			int numParams = static_cast<int>(params.Size());

			alignas(16) u8 stackParamStorage[kMaxStackParamStorage];
			void*          stackParamPointers[kMaxStackParams];

			Array<u8>    heapParamStorage;
			Array<void*> heapParamPointers;

			usize totalStorageNeeded = 0;
			for (int i = 0; i < numParams && i < argc; i++)
			{
				const FieldProps& paramProps = params[i]->GetProps();
				usize alignment = paramProps.alignment > 0 ? paramProps.alignment : 1;
				totalStorageNeeded = (totalStorageNeeded + alignment - 1) & ~(alignment - 1);
				totalStorageNeeded += paramProps.size;
			}

			bool useHeap = (numParams > static_cast<int>(kMaxStackParams)) ||
			               (totalStorageNeeded > kMaxStackParamStorage);

			u8*    paramStoragePtr = useHeap ? nullptr : stackParamStorage;
			void** paramPointersPtr = useHeap ? nullptr : stackParamPointers;

			if (useHeap)
			{
				heapParamStorage.Resize(totalStorageNeeded);
				heapParamPointers.Reserve(numParams);
				paramStoragePtr = heapParamStorage.Data();
			}

			usize currentOffset = 0;
			int convertedCount = 0;
			for (int i = 0; i < numParams && i < argc; i++)
			{
				const FieldProps& paramProps = params[i]->GetProps();
				usize alignment = paramProps.alignment > 0 ? paramProps.alignment : 1;
				usize alignedOffset = (currentOffset + alignment - 1) & ~(alignment - 1);

				void* paramPtr = paramStoragePtr + alignedOffset;

				if (useHeap)
				{
					heapParamPointers.EmplaceBack(paramPtr);
				}
				else
				{
					paramPointersPtr[i] = paramPtr;
				}

				if (!LuaToCpp(L, i + 1, paramProps, paramPtr))
				{
					DestroyConvertedParams(useHeap ? heapParamPointers.Data() : paramPointersPtr, params, convertedCount);
					MemFree(memory);
					return luaL_error(L, "Cannot convert constructor argument %d for %s", i, reflType->GetSimpleName().CStr());
				}

				convertedCount++;
				currentOffset = alignedOffset + paramProps.size;
			}

			void** finalParamPointers = useHeap ? heapParamPointers.Data() : paramPointersPtr;
			ctor->Construct(memory, (numParams == 0 || argc == 0) ? nullptr : finalParamPointers);

			DestroyConvertedParams(finalParamPointers, params, convertedCount);

			// Wrap in userdata
			auto metaIt = typeIdToMetatableName.Find(reflType->GetProps().typeId);
			if (!metaIt)
			{
				MemFree(memory);
				return luaL_error(L, "No metatable registered for %s", reflType->GetSimpleName().CStr());
			}

			LuaSkoreObject* ud = static_cast<LuaSkoreObject*>(lua_newuserdatauv(L, sizeof(LuaSkoreObject), 0));
			ud->instance = memory;
			ud->type = reflType;
			ud->ownsMemory = true;
			luaL_setmetatable(L, metaIt->second.CStr());

			return 1;
		}

		// ----------------------------------------------------------------
		// Static method call closure
		// ----------------------------------------------------------------

		int LuaStaticMethod_call(lua_State* L)
		{
			Array<ReflectFunction*>* funcArr = static_cast<Array<ReflectFunction*>*>(lua_touserdata(L, lua_upvalueindex(1)));
			int argc = lua_gettop(L);

			ReflectFunction* func = nullptr;
			for (ReflectFunction* f : *funcArr)
			{
				if (f->IsStatic() && static_cast<int>(f->GetParams().Size()) == argc)
				{
					func = f;
					break;
				}
			}

			if (!func && !funcArr->Empty())
			{
				for (ReflectFunction* f : *funcArr)
				{
					if (f->IsStatic())
					{
						func = f;
						break;
					}
				}
			}

			if (!func)
			{
				luaBindingsLogger.Error("No matching static method overload");
				return 0;
			}

			bool hasReturn = InvokeReflectFunction(L, func, nullptr, 1, argc);
			return hasReturn ? 1 : 0;
		}

		// ----------------------------------------------------------------
		// Bind a single reflected type
		// ----------------------------------------------------------------

		void BindReflectedType(lua_State* L, ReflectType* reflType)
		{
			StringView typeName = reflType->GetSimpleName();
			if (typeName.Empty()) return;
			if (typeName.FindFirstOf('<') != StringView::s_npos) return;

			String metatableName(typeName);

			// Enum types -> simple table with integer values
			if (reflType->GetProps().isEnum)
			{
				lua_newtable(L);
				for (ReflectValue* value : reflType->GetValues())
				{
					lua_pushinteger(L, value->GetCode());
					lua_setfield(L, -2, value->GetDesc().CStr());
				}
				lua_setglobal(L, metatableName.CStr());
				typeIdToMetatableName.Insert(reflType->GetProps().typeId, metatableName);
				luaBindingsLogger.Debug("Bound enum type: {} with {} values", typeName, reflType->GetValues().Size());
				return;
			}

			// Register metatable
			typeIdToMetatableName.Insert(reflType->GetProps().typeId, metatableName);

			luaL_newmetatable(L, metatableName.CStr());

			// Build cache (heap-allocated for pointer stability)
			LuaTypeBindingCache* cache = new LuaTypeBindingCache();

			for (ReflectField* field : reflType->GetFields())
			{
				cache->fieldsByName.Insert(String(field->GetName()), field);
			}

			HashSet<StringView> seenFuncNames;
			for (ReflectFunction* func : reflType->GetFunctions())
			{
				StringView funcName = func->GetName();
				if (funcName.Empty() || seenFuncNames.Has(funcName)) continue;
				seenFuncNames.Insert(funcName);

				Span<ReflectFunction*> overloads = reflType->FindFunctionByName(funcName);
				Array<ReflectFunction*> overloadsCopy;
				overloadsCopy.Reserve(overloads.Size());
				for (ReflectFunction* f : overloads)
				{
					overloadsCopy.EmplaceBack(f);
				}
				cache->functionsByName.Emplace(String(funcName), Traits::Move(overloadsCopy));
			}

			typeBindingCaches.Insert(metatableName, cache);

			// __gc
			lua_pushcfunction(L, LuaSkoreObject_gc);
			lua_setfield(L, -2, "__gc");

			// __tostring
			lua_pushcfunction(L, LuaSkoreObject_tostring);
			lua_setfield(L, -2, "__tostring");

			// __index with cache upvalue
			lua_pushlightuserdata(L, cache);
			lua_pushcclosure(L, LuaSkoreObject_index, 1);
			lua_setfield(L, -2, "__index");

			// __newindex with cache upvalue
			lua_pushlightuserdata(L, cache);
			lua_pushcclosure(L, LuaSkoreObject_newindex, 1);
			lua_setfield(L, -2, "__newindex");

			lua_pop(L, 1); // pop metatable

			// Create a global table for the type (constructor + static methods)
			bool hasConstructor = (reflType->GetDefaultConstructor() != nullptr) || (!reflType->GetConstructors().Empty());
			bool hasStaticMethods = false;
			for (ReflectFunction* func : reflType->GetFunctions())
			{
				if (func->IsStatic())
				{
					hasStaticMethods = true;
					break;
				}
			}

			if (hasConstructor || hasStaticMethods)
			{
				lua_newtable(L); // The type table

				// Store __typeId so GetComponent(Transform) works
				lua_pushinteger(L, static_cast<lua_Integer>(reflType->GetProps().typeId.id));
				lua_setfield(L, -2, "__typeId");

				// Add static methods
				HashSet<StringView> seenStaticNames;
				for (ReflectFunction* func : reflType->GetFunctions())
				{
					if (func->IsStatic())
					{
						StringView funcName = func->GetName();
						if (funcName.Empty() || seenStaticNames.Has(funcName)) continue;
						seenStaticNames.Insert(funcName);

						auto funcIt = cache->functionsByName.Find(funcName);
						if (!funcIt) continue;

						lua_pushlightuserdata(L, &funcIt->second);
						lua_pushcclosure(L, LuaStaticMethod_call, 1);
						lua_setfield(L, -2, funcName.CStr());
					}
				}

				if (hasConstructor)
				{
					// Add __call metamethod so TypeName(args) works as constructor
					lua_newtable(L);  // metatable for the type table
					lua_pushlightuserdata(L, reflType);
					lua_pushcclosure(L, [](lua_State* L) -> int
					{
						// __call receives the table as arg 1, then constructor args
						// Remove the table, shift args
						lua_remove(L, 1);
						return LuaSkoreObject_new(L);
					}, 1);
					lua_setfield(L, -2, "__call");
					lua_setmetatable(L, -2); // set metatable on type table
				}

				lua_setglobal(L, metatableName.CStr());
			}

			luaBindingsLogger.Debug("Bound type: {}", typeName);
		}

		// ----------------------------------------------------------------
		// Print override
		// ----------------------------------------------------------------

		int LuaPrintOverride(lua_State* L)
		{
			int n = lua_gettop(L);
			String msg;
			for (int i = 1; i <= n; i++)
			{
				if (i > 1) msg += "\t";
				const char* s = luaL_tolstring(L, i, nullptr);
				if (s) msg += s;
				lua_pop(L, 1); // pop the tolstring result
			}
			luaBindingsLogger.Info("{}", msg);
			return 0;
		}
	}

	// ----------------------------------------------------------------
	// Public API implementations
	// ----------------------------------------------------------------

	TypeID LuaResourceLoader::GetResourceTypeId()
	{
			return sktypeid(LuaScriptResource);
	}

	void LuaResourceLoader::LoadResource(RID rid)
	{
		ResourceObject object = Resources::Read(rid);
		Span<u8> scriptBinary = object.GetBlob(LuaScriptResource::Binary);
		ScriptManager::LoadScript(scriptBinary, ".lua");
	}

	bool LuaCppToLua(lua_State* L, const void* cppVal, const FieldProps& props)
	{
		return CppToLua(L, cppVal, props);
	}

	bool LuaLuaToCpp(lua_State* L, int index, const FieldProps& props, void* outBuffer)
	{
		return LuaToCpp(L, index, props, outBuffer);
	}

	void InitLuaBindings(lua_State* L)
	{
		luaBindingsLogger.Debug("Initializing Lua bindings for reflected types...");

		Array<ReflectType*> allTypes = Reflection::GetAllTypes();
		int boundCount = 0;

		for (ReflectType* type : allTypes)
		{
			if (type)
			{
				StringView name = type->GetSimpleName();
				if (!name.Empty() && name.FindFirstOf('<') == StringView::s_npos)
				{
					BindReflectedType(L, type);
					boundCount++;
				}
			}
		}

		luaBindingsLogger.Debug("Bound {} types to Lua", boundCount);
	}
}


// ============================================================================
// EmmyLua Annotation Generator
// ============================================================================

namespace Skore
{
	namespace
	{
		Logger& luaAnnotationLogger = Logger::GetLogger("Skore::LuaAnnotations");

		String GetLuaTypeHint(const FieldProps& props, const HashMap<TypeID, String>& typeNameMap)
		{
			TypeID typeId = props.typeId;

			if (typeId.id == 0 || typeId == TypeInfo<void>::ID())
			{
				return "nil";
			}

			if (props.isEnum)
			{
				if (auto it = typeNameMap.Find(typeId))
				{
					return it->second;
				}
				return "integer";
			}

			if (typeId == TypeInfo<bool>::ID())
			{
				return "boolean";
			}

			if (typeId == TypeInfo<i8>::ID() || typeId == TypeInfo<i16>::ID() ||
			    typeId == TypeInfo<i32>::ID() || typeId == TypeInfo<i64>::ID() ||
			    typeId == TypeInfo<u8>::ID() || typeId == TypeInfo<u16>::ID() ||
			    typeId == TypeInfo<u32>::ID() || typeId == TypeInfo<u64>::ID())
			{
				return "integer";
			}

			if (typeId == TypeInfo<f32>::ID() || typeId == TypeInfo<f64>::ID())
			{
				return "number";
			}

			if (typeId == TypeInfo<String>::ID() || typeId == TypeInfo<StringView>::ID())
			{
				return "string";
			}

			if (typeId == TypeInfo<TypeID>::ID())
			{
				return "integer";
			}

			if (props.typeApi == TypeInfo<ArrayApi>::ID() && props.getTypeApi)
			{
				ArrayApi api{};
				props.getTypeApi(&api);
				TypeProps elemProps = api.GetProps();
				FieldProps elemFieldProps = ToFieldProps(elemProps);
				String elemHint = GetLuaTypeHint(elemFieldProps, typeNameMap);
				return elemHint + "[]";
			}

			if (props.typeApi == TypeInfo<SpanApi>::ID() && props.getTypeApi)
			{
				SpanApi api{};
				props.getTypeApi(&api);
				TypeProps elemProps = api.GetProps();
				FieldProps elemFieldProps = ToFieldProps(elemProps);
				String elemHint = GetLuaTypeHint(elemFieldProps, typeNameMap);
				return elemHint + "[]";
			}

			if (props.typeApi == TypeInfo<HashMapApi>::ID() && props.getTypeApi)
			{
				HashMapApi api{};
				props.getTypeApi(&api);
				TypeProps keyProps = api.GetKeyProps();
				TypeProps valProps = api.GetValueProps();
				FieldProps keyFieldProps = ToFieldProps(keyProps);
				FieldProps valFieldProps = ToFieldProps(valProps);
				String keyHint = GetLuaTypeHint(keyFieldProps, typeNameMap);
				String valHint = GetLuaTypeHint(valFieldProps, typeNameMap);
				return "table<" + keyHint + ", " + valHint + ">";
			}

			if (auto it = typeNameMap.Find(typeId))
			{
				return it->second;
			}

			return "any";
		}

		String EscapeLuaKeyword(StringView name)
		{
			static const HashSet<StringView> luaKeywords = {
				"and", "break", "do", "else", "elseif", "end",
				"false", "for", "function", "goto", "if", "in",
				"local", "nil", "not", "or", "repeat", "return",
				"then", "true", "until", "while"
			};

			if (luaKeywords.Has(name))
			{
				return String(name) + "_";
			}
			return String(name);
		}

		void GenerateLuaEnumAnnotation(ReflectType* type, String& output)
		{
			StringView typeName = type->GetSimpleName();

			output += "---@enum ";
			output += typeName;
			output += "\n";
			output += typeName;
			output += " = {\n";

			Span<ReflectValue*> values = type->GetValues();
			for (usize i = 0; i < values.Size(); i++)
			{
				ReflectValue* value = values[i];
				StringView valueDesc = value->GetDesc();
				i64 valueCode = value->GetCode();

				output += "    ";
				output += valueDesc;
				output += " = ";
				output += std::to_string(valueCode).c_str();
				if (i + 1 < values.Size())
				{
					output += ",";
				}
				output += "\n";
			}

			output += "}\n\n";
		}

		void CollectLuaBaseMembers(ReflectType* type, HashSet<String>& baseFieldNames, HashSet<String>& baseFuncNames)
		{
			Span<TypeID> bases = type->GetBaseTypes();
			for (const TypeID& baseId : bases)
			{
				ReflectType* baseType = Reflection::FindTypeById(baseId);
				if (!baseType) continue;

				for (ReflectField* field : baseType->GetFields())
				{
					baseFieldNames.Insert(String(field->GetName()));
				}

				for (ReflectFunction* func : baseType->GetFunctions())
				{
					baseFuncNames.Insert(String(func->GetSimpleName()));
				}
			}
		}

		void GenerateLuaClassAnnotation(ReflectType* type, String& output, const HashMap<TypeID, String>& typeNameMap)
		{
			StringView typeName = type->GetSimpleName();

			// Determine base class
			Span<TypeID> bases = type->GetBaseTypes();
			String baseClassName;
			if (!bases.Empty())
			{
				if (auto it = typeNameMap.Find(bases[0]))
				{
					baseClassName = it->second;
				}
			}

			// Class annotation
			output += "---@class ";
			output += typeName;
			if (!baseClassName.Empty())
			{
				output += " : ";
				output += baseClassName;
			}
			output += "\n";

			// Collect inherited members
			HashSet<String> baseFieldNames;
			HashSet<String> baseFuncNames;
			CollectLuaBaseMembers(type, baseFieldNames, baseFuncNames);

			// Fields
			Span<ReflectField*> fields = type->GetFields();
			for (ReflectField* field : fields)
			{
				StringView fieldName = field->GetName();
				if (baseFieldNames.Has(String(fieldName))) continue;

				const FieldProps& fieldProps = field->GetProps();
				String fieldType = GetLuaTypeHint(fieldProps, typeNameMap);

				output += "---@field ";
				output += EscapeLuaKeyword(fieldName);
				output += " ";
				output += fieldType;
				output += "\n";
			}

			// Constructor overloads
			Span<ReflectConstructor*> constructors = type->GetConstructors();
			if (!constructors.Empty())
			{
				for (ReflectConstructor* ctor : constructors)
				{
					Span<ReflectParam*> params = ctor->GetParams();
					output += "---@overload fun(";
					for (usize i = 0; i < params.Size(); i++)
					{
						if (i > 0) output += ", ";
						ReflectParam* param = params[i];
						const FieldProps& paramProps = param->GetProps();
						String paramName = EscapeLuaKeyword(param->GetName());
						String paramType = GetLuaTypeHint(paramProps, typeNameMap);
						output += paramName;
						output += ": ";
						output += paramType;
					}
					output += "): ";
					output += typeName;
					output += "\n";
				}
			}

			// Class table declaration
			output += typeName;
			output += " = {}\n\n";

			// Methods
			Span<ReflectFunction*> functions = type->GetFunctions();
			HashMap<String, Array<ReflectFunction*>> methodsByName;
			for (ReflectFunction* func : functions)
			{
				StringView funcName = func->GetSimpleName();
				if (funcName.Empty()) continue;
				if (baseFuncNames.Has(String(funcName))) continue;

				String nameStr(funcName);
				if (!methodsByName.Has(nameStr))
				{
					methodsByName.Insert(nameStr, Array<ReflectFunction*>());
				}
				methodsByName[nameStr].EmplaceBack(func);
			}

			for (auto& it : methodsByName)
			{
				const String& methodName = it.first;
				Array<ReflectFunction*>& overloads = it.second;

				for (ReflectFunction* func : overloads)
				{
					bool isStatic = func->IsStatic();
					Span<ReflectParam*> params = func->GetParams();
					FieldProps returnProps = func->GetReturn();
					String returnType = GetLuaTypeHint(returnProps, typeNameMap);

					// Parameter annotations
					for (usize i = 0; i < params.Size(); i++)
					{
						ReflectParam* param = params[i];
						const FieldProps& paramProps = param->GetProps();
						String paramName = EscapeLuaKeyword(param->GetName());
						String paramType = GetLuaTypeHint(paramProps, typeNameMap);

						output += "---@param ";
						output += paramName;
						output += " ";
						output += paramType;
						output += "\n";
					}

					// Return annotation
					if (returnProps.typeId.id != 0 && returnProps.typeId != TypeInfo<void>::ID())
					{
						output += "---@return ";
						output += returnType;
						output += "\n";
					}

					// Function signature
					output += "function ";
					output += typeName;
					if (isStatic)
					{
						output += ".";
					}
					else
					{
						output += ":";
					}
					output += EscapeLuaKeyword(methodName);
					output += "(";

					for (usize i = 0; i < params.Size(); i++)
					{
						if (i > 0) output += ", ";
						output += EscapeLuaKeyword(params[i]->GetName());
					}

					output += ") end\n\n";
				}
			}
		}

		bool ShouldIncludeLuaType(ReflectType* type)
		{
			if (!type) return false;

			StringView name = type->GetSimpleName();
			if (name.Empty()) return false;

			if (name.FindFirstOf('<') != StringView::s_npos) return false;

			if (name[0] == '_') return false;

			return true;
		}
	}

	void GenerateLuaAnnotations(StringView outputPath)
	{
		luaAnnotationLogger.Info("Generating Lua annotations to: {}", outputPath);

		FileStatus status = FileSystem::GetFileStatus(outputPath);
		if (!status.exists)
		{
			FileSystem::CreateDirectory(outputPath);
		}

		Array<ReflectType*> allTypes = Reflection::GetAllTypes();

		HashMap<String, ReflectType*> latestBySimpleName;
		for (ReflectType* type : allTypes)
		{
			if (!ShouldIncludeLuaType(type)) continue;
			if (IsLuaType(type->GetProps().typeId)) continue;

			String simpleName(type->GetSimpleName());
			latestBySimpleName[simpleName] = type;
		}

		HashMap<TypeID, String> typeNameMap;
		for (auto& it : latestBySimpleName)
		{
			typeNameMap.Insert(it.second->GetProps().typeId, it.first);
		}

		Array<ReflectType*> enumTypes;
		Array<ReflectType*> classTypes;

		for (auto& it : latestBySimpleName)
		{
			ReflectType* type = it.second;
			if (type->GetProps().isEnum)
			{
				enumTypes.EmplaceBack(type);
			}
			else
			{
				classTypes.EmplaceBack(type);
			}
		}

		String content;

		content += "--- Auto-generated EmmyLua annotations for Skore engine\n";
		content += "--- Do not edit manually - regenerate using GenerateLuaAnnotations()\n\n";

		// Global functions
		content += "---Register a new component class with Skore.\n";
		content += "---@param name string\n";
		content += "---@param definition table\n";
		content += "function RegisterComponent(name, definition) end\n\n";

		content += "---Print values to the console.\n";
		content += "---@vararg any\n";
		content += "function print(...) end\n\n";

		// Enums
		if (!enumTypes.Empty())
		{
			content += "-- Enums\n\n";
			for (ReflectType* type : enumTypes)
			{
				GenerateLuaEnumAnnotation(type, content);
			}
		}

		// Classes
		if (!classTypes.Empty())
		{
			content += "-- Classes\n\n";
			for (ReflectType* type : classTypes)
			{
				GenerateLuaClassAnnotation(type, content, typeNameMap);
			}
		}

		// Write file
		String filePath = String(outputPath);
		if (!filePath.Empty())
		{
			char lastChar = filePath[filePath.Size() - 1];
			if (lastChar != '/' && lastChar != '\\')
			{
				filePath += "/";
			}
		}
		filePath += "skore.lua";

		FileSystem::SaveFileAsString(filePath, content);
		luaAnnotationLogger.Info("Generated annotation file: {} ({} enums, {} classes)",
		                         filePath, enumTypes.Size(), classTypes.Size());
	}
}


// ============================================================================
// Section 2: Lua Type Registry (RegisterComponent)
// ============================================================================

namespace Skore
{
	// Forward declaration (defined in Section 3 after LuaScriptEngine)
	void UntrackLuaComponentInstance(LuaComponentWrapper* wrapper);

	namespace
	{
		Logger& luaRegistryLogger = Logger::GetLogger("Skore::LuaTypeRegistry");

		// --- Data Structures ---

		struct LuaExportedField
		{
			String     name;
			TypeID     typeId;
			FieldProps fieldProps;
			int        defaultValueRef = LUA_NOREF;
			bool       resolved = false;
		};

		struct LuaRegisteredType
		{
			TypeID               typeId;
			String               name;
			int                  classTableRef = LUA_NOREF;
			ReflectType*         reflectType = nullptr;
			Array<LuaExportedField> fields;
			Array<LuaComponentWrapper*> liveInstances;
		};

		struct LuaFieldAccessData
		{
			String fieldName;
			usize  fieldIndex;
			usize  registeredTypeIndex;
		};

		Array<LuaRegisteredType>                  registeredLuaTypes;
		HashMap<TypeID, usize>                    typeIdToLuaTypeIndex;
		HashMap<const ReflectField*, LuaFieldAccessData> luaFieldAccessDataMap;

		// --- Forward Declarations ---

		void RegisterLuaClass(lua_State* L, usize typeIndex);

		// --- Field Access Callbacks ---

		void LuaWrapperFieldGet(const ReflectField* field, ConstPtr instance, VoidPtr dest, usize destSize)
		{
			auto it = luaFieldAccessDataMap.Find(field);
			if (!it) return;

			const LuaFieldAccessData& accessData = it->second;
			const LuaComponentWrapper* wrapper = static_cast<const LuaComponentWrapper*>(instance);
			lua_State* L = wrapper->GetLuaState();
			int selfRef = wrapper->GetSelfRef();
			if (!L || selfRef == LUA_NOREF) return;

			lua_rawgeti(L, LUA_REGISTRYINDEX, selfRef);
			if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
			lua_getfield(L, -1, accessData.fieldName.CStr());
			LuaToCpp(L, -1, field->GetProps(), dest);
			lua_pop(L, 2);
		}

		void LuaWrapperFieldSet(const ReflectField* field, VoidPtr instance, ConstPtr src, usize srcSize)
		{
			auto it = luaFieldAccessDataMap.Find(field);
			if (!it) return;

			const LuaFieldAccessData& accessData = it->second;
			LuaComponentWrapper* wrapper = static_cast<LuaComponentWrapper*>(instance);
			lua_State* L = wrapper->GetLuaState();
			int selfRef = wrapper->GetSelfRef();
			if (!L || selfRef == LUA_NOREF) return;

			lua_rawgeti(L, LUA_REGISTRYINDEX, selfRef);
			if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
			CppToLua(L, src, field->GetProps());
			lua_setfield(L, -2, accessData.fieldName.CStr());
			lua_pop(L, 1);
		}

		void LuaWrapperFieldCopy(const ReflectField* field, ConstPtr src, VoidPtr dest)
		{
			auto it = luaFieldAccessDataMap.Find(field);
			if (!it) return;

			const LuaFieldAccessData& accessData = it->second;
			const LuaComponentWrapper* srcWrapper = static_cast<const LuaComponentWrapper*>(src);
			LuaComponentWrapper* destWrapper = static_cast<LuaComponentWrapper*>(dest);

			lua_State* L = srcWrapper->GetLuaState();
			int srcRef = srcWrapper->GetSelfRef();
			int destRef = destWrapper->GetSelfRef();
			if (!L || srcRef == LUA_NOREF || destRef == LUA_NOREF) return;

			lua_rawgeti(L, LUA_REGISTRYINDEX, srcRef);
			if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
			lua_getfield(L, -1, accessData.fieldName.CStr());

			lua_rawgeti(L, LUA_REGISTRYINDEX, destRef);
			if (!lua_istable(L, -1)) { lua_pop(L, 3); return; }
			lua_pushvalue(L, -2); // push the value
			lua_setfield(L, -2, accessData.fieldName.CStr());

			lua_pop(L, 3); // pop dest table, value, src table
		}

		// --- Resource Serialization ---

		void LuaFieldToResourceImpl(const ReflectField* field, ResourceObject& resourceObject, u32 index, ConstPtr instance, UndoRedoScope* scope, VoidPtr userData)
		{
			const FieldProps& props = field->GetProps();
			TypeID typeId = props.typeId;

			if (typeId == TypeInfo<bool>::ID())
			{
				bool value = false;
				field->Get(instance, &value, sizeof(value));
				resourceObject.SetBool(index, value);
			}
			else if (typeId == TypeInfo<i64>::ID() || typeId == TypeInfo<i32>::ID())
			{
				i64 value = 0;
				if (typeId == TypeInfo<i32>::ID())
				{
					i32 tmp = 0;
					field->Get(instance, &tmp, sizeof(tmp));
					value = tmp;
				}
				else
				{
					field->Get(instance, &value, sizeof(value));
				}
				resourceObject.SetInt(index, value);
			}
			else if (typeId == TypeInfo<f64>::ID() || typeId == TypeInfo<f32>::ID())
			{
				f64 value = 0.0;
				if (typeId == TypeInfo<f32>::ID())
				{
					f32 tmp = 0.0f;
					field->Get(instance, &tmp, sizeof(tmp));
					value = tmp;
				}
				else
				{
					field->Get(instance, &value, sizeof(value));
				}
				resourceObject.SetFloat(index, value);
			}
			else if (typeId == TypeInfo<String>::ID())
			{
				String value;
				field->Get(instance, &value, sizeof(value));
				resourceObject.SetString(index, value);
			}
			else if (typeId == TypeInfo<Vec2>::ID())
			{
				Vec2 value{};
				field->Get(instance, &value, sizeof(value));
				resourceObject.SetVec2(index, value);
			}
			else if (typeId == TypeInfo<Vec3>::ID())
			{
				Vec3 value{};
				field->Get(instance, &value, sizeof(value));
				resourceObject.SetVec3(index, value);
			}
			else if (typeId == TypeInfo<Vec4>::ID())
			{
				Vec4 value{};
				field->Get(instance, &value, sizeof(value));
				resourceObject.SetVec4(index, value);
			}
			else if (typeId == TypeInfo<Quat>::ID())
			{
				Quat value{};
				field->Get(instance, &value, sizeof(value));
				resourceObject.SetQuat(index, value);
			}
			else if (typeId == TypeInfo<Mat4>::ID())
			{
				Mat4 value{};
				field->Get(instance, &value, sizeof(value));
				resourceObject.SetMat4(index, value);
			}
			else if (typeId == TypeInfo<Color>::ID())
			{
				Color value{};
				field->Get(instance, &value, sizeof(value));
				resourceObject.SetColor(index, value);
			}
		}

		void LuaFieldFromResourceImpl(const ReflectField* field, const ResourceObject& resourceObject, u32 index, VoidPtr instance, VoidPtr userData)
		{
			const FieldProps& props = field->GetProps();
			TypeID typeId = props.typeId;

			if (typeId == TypeInfo<bool>::ID())
			{
				bool value = resourceObject.GetBool(index);
				field->Set(instance, &value, sizeof(value));
			}
			else if (typeId == TypeInfo<i64>::ID() || typeId == TypeInfo<i32>::ID())
			{
				i64 value = resourceObject.GetInt(index);
				if (typeId == TypeInfo<i32>::ID())
				{
					i32 tmp = static_cast<i32>(value);
					field->Set(instance, &tmp, sizeof(tmp));
				}
				else
				{
					field->Set(instance, &value, sizeof(value));
				}
			}
			else if (typeId == TypeInfo<f64>::ID() || typeId == TypeInfo<f32>::ID())
			{
				f64 value = resourceObject.GetFloat(index);
				if (typeId == TypeInfo<f32>::ID())
				{
					f32 tmp = static_cast<f32>(value);
					field->Set(instance, &tmp, sizeof(tmp));
				}
				else
				{
					field->Set(instance, &value, sizeof(value));
				}
			}
			else if (typeId == TypeInfo<String>::ID())
			{
				String value(resourceObject.GetString(index));
				field->Set(instance, &value, sizeof(value));
			}
			else if (typeId == TypeInfo<Vec2>::ID())
			{
				Vec2 value = resourceObject.GetVec2(index);
				field->Set(instance, &value, sizeof(value));
			}
			else if (typeId == TypeInfo<Vec3>::ID())
			{
				Vec3 value = resourceObject.GetVec3(index);
				field->Set(instance, &value, sizeof(value));
			}
			else if (typeId == TypeInfo<Vec4>::ID())
			{
				Vec4 value = resourceObject.GetVec4(index);
				field->Set(instance, &value, sizeof(value));
			}
			else if (typeId == TypeInfo<Quat>::ID())
			{
				Quat value = resourceObject.GetQuat(index);
				field->Set(instance, &value, sizeof(value));
			}
			else if (typeId == TypeInfo<Mat4>::ID())
			{
				Mat4 value = resourceObject.GetMat4(index);
				field->Set(instance, &value, sizeof(value));
			}
			else if (typeId == TypeInfo<Color>::ID())
			{
				Color value = resourceObject.GetColor(index);
				field->Set(instance, &value, sizeof(value));
			}
		}

		ResourceFieldInfo LuaFieldGetResourceFieldInfoImpl(const ReflectField* field)
		{
			const FieldProps& props = field->GetProps();
			TypeID typeId = props.typeId;

			if (typeId == TypeInfo<bool>::ID())
				return {ResourceFieldType::Bool, typeId};
			if (typeId == TypeInfo<i64>::ID() || typeId == TypeInfo<i32>::ID())
				return {ResourceFieldType::Int, typeId};
			if (typeId == TypeInfo<f64>::ID() || typeId == TypeInfo<f32>::ID())
				return {ResourceFieldType::Float, typeId};
			if (typeId == TypeInfo<String>::ID())
				return {ResourceFieldType::String, typeId};
			if (typeId == TypeInfo<Vec2>::ID())
				return {ResourceFieldType::Vec2, typeId};
			if (typeId == TypeInfo<Vec3>::ID())
				return {ResourceFieldType::Vec3, typeId};
			if (typeId == TypeInfo<Vec4>::ID())
				return {ResourceFieldType::Vec4, typeId};
			if (typeId == TypeInfo<Quat>::ID())
				return {ResourceFieldType::Quat, typeId};
			if (typeId == TypeInfo<Mat4>::ID())
				return {ResourceFieldType::Mat4, typeId};
			if (typeId == TypeInfo<Color>::ID())
				return {ResourceFieldType::Color, typeId};

			if (auto it = typeIdToLuaTypeIndex.Find(typeId))
				return {ResourceFieldType::SubObject, typeId};

			return {ResourceFieldType::None, {}};
		}

		// --- LuaComponentWrapper Lifecycle Functions ---

		void RemoveFromLiveInstances(LuaComponentWrapper* wrapper)
		{
			if (!App::ReloadedEnabled()) return;

			TypeID typeId = wrapper->GetReflectTypeId();
			if (auto it = typeIdToLuaTypeIndex.Find(typeId))
			{
				Array<LuaComponentWrapper*>& arr = registeredLuaTypes[it->second].liveInstances;
				for (usize i = 0; i < arr.Size(); ++i)
				{
					if (arr[i] == wrapper)
					{
						arr.Erase(arr.begin() + i);
						break;
					}
				}
			}
		}

		void LuaComponentWrapperDestructor(const ReflectType* type, VoidPtr instance)
		{
			LuaComponentWrapper* wrapper = static_cast<LuaComponentWrapper*>(instance);
			RemoveFromLiveInstances(wrapper);
			wrapper->~LuaComponentWrapper();
		}

		void LuaComponentWrapperDestroy(const ReflectType* type, Allocator* allocator, VoidPtr instance)
		{
			LuaComponentWrapperDestructor(type, instance);
			allocator->MemFree(allocator->allocator, instance);
		}

		void InitLuaComponentWrapperInstance(LuaComponentWrapper* wrapper, lua_State* L, usize typeIndex)
		{
			LuaRegisteredType& regType = registeredLuaTypes[typeIndex];

			wrapper->SetLuaState(L);
			wrapper->SetReflectTypeId(regType.typeId);

			// Create instance table
			lua_newtable(L);

			// Copy default values
			for (const LuaExportedField& field : regType.fields)
			{
				if (field.defaultValueRef != LUA_NOREF)
				{
					lua_rawgeti(L, LUA_REGISTRYINDEX, field.defaultValueRef);
					lua_setfield(L, -2, field.name.CStr());
				}
			}

			// Create metatable with __index = class table
			lua_newtable(L);
			lua_rawgeti(L, LUA_REGISTRYINDEX, regType.classTableRef);
			lua_setfield(L, -2, "__index");
			lua_setmetatable(L, -2);

			// Store instance table in registry
			wrapper->SetSelfRef(luaL_ref(L, LUA_REGISTRYINDEX));
			wrapper->SetClassTableRef(regType.classTableRef);

			// Track live instance for hot reload
			if (App::ReloadedEnabled())
			{
				regType.liveInstances.EmplaceBack(wrapper);
			}
		}

		void LuaComponentWrapperPlacementNew(const ReflectConstructor* ctor, VoidPtr memory, VoidPtr* params)
		{
			TypeID typeId;
			typeId.id = reinterpret_cast<u64>(ctor->GetUserData());

			auto it = typeIdToLuaTypeIndex.Find(typeId);
			if (!it)
			{
				new(PlaceHolder{}, memory) LuaComponentWrapper();
				return;
			}

			lua_State* L = GetLuaState();
			LuaComponentWrapper* wrapper = new(PlaceHolder{}, memory) LuaComponentWrapper();
			InitLuaComponentWrapperInstance(wrapper, L, it->second);
		}

		Object* LuaComponentWrapperNewObject(const ReflectConstructor* ctor, Allocator* allocator, VoidPtr* params)
		{
			TypeID typeId;
			typeId.id = reinterpret_cast<u64>(ctor->GetUserData());

			auto it = typeIdToLuaTypeIndex.Find(typeId);
			if (!it) return nullptr;

			lua_State* L = GetLuaState();
			LuaComponentWrapper* wrapper = new(allocator->MemAlloc(allocator->allocator, sizeof(LuaComponentWrapper))) LuaComponentWrapper();
			InitLuaComponentWrapperInstance(wrapper, L, it->second);

			return wrapper;
		}

		// --- RegisterComponent C-function ---

		FieldProps InferFieldPropsFromLua(lua_State* L, int index)
		{
			FieldProps props{};

			if (lua_isinteger(L, index))
			{
				props.typeId = TypeInfo<i32>::ID();
				props.size = sizeof(i32);
				props.alignment = alignof(i32);
			}
			else if (lua_isnumber(L, index))
			{
				props.typeId = TypeInfo<f32>::ID();
				props.size = sizeof(f32);
				props.alignment = alignof(f32);
			}
			else if (lua_isboolean(L, index))
			{
				props.typeId = TypeInfo<bool>::ID();
				props.size = sizeof(bool);
				props.alignment = alignof(bool);
			}
			else if (lua_isstring(L, index))
			{
				props.typeId = TypeInfo<String>::ID();
				props.size = sizeof(String);
				props.alignment = alignof(String);
			}
			else if (lua_isuserdata(L, index))
			{
				LuaSkoreObject* obj = static_cast<LuaSkoreObject*>(lua_touserdata(L, index));
				if (obj && obj->type)
				{
					const TypeProps& tprops = obj->type->GetProps();
					props.typeId = tprops.typeId;
					props.size = tprops.size;
					props.alignment = tprops.alignment;
				}
			}

			return props;
		}

		int RegisterComponent_cfunc(lua_State* L)
		{
			const char* name = luaL_checkstring(L, 1);
			luaL_checktype(L, 2, LUA_TTABLE);

			// Generate TypeID from name hash
			TypeID typeId;
			typeId.id = Hash<StringView>::Value(StringView(name));

			bool isNew = false;
			auto existingIt = typeIdToLuaTypeIndex.Find(typeId);

			// If not registered yet, create the entry
			if (!existingIt)
			{
				usize typeIndex = registeredLuaTypes.Size();
				LuaRegisteredType& regType = registeredLuaTypes.EmplaceBack();
				regType.typeId = typeId;
				regType.name = String(name);
				typeIdToLuaTypeIndex.Insert(typeId, typeIndex);
				isNew = true;
				existingIt = typeIdToLuaTypeIndex.Find(typeId);
			}

			usize typeIndex = existingIt->second;
			LuaRegisteredType& regType = registeredLuaTypes[typeIndex];

			// Release old class table ref
			if (regType.classTableRef != LUA_NOREF)
			{
				luaL_unref(L, LUA_REGISTRYINDEX, regType.classTableRef);
			}

			// Store new class table ref
			lua_pushvalue(L, 2);
			regType.classTableRef = luaL_ref(L, LUA_REGISTRYINDEX);

			// Clear old fields and rebuild
			for (auto& f : regType.fields)
			{
				if (f.defaultValueRef != LUA_NOREF)
				{
					luaL_unref(L, LUA_REGISTRYINDEX, f.defaultValueRef);
				}
			}
			regType.fields.Clear();

			// Scan table for fields (skip functions)
			lua_pushnil(L);
			while (lua_next(L, 2) != 0)
			{
				if (lua_isstring(L, -2) && !lua_isfunction(L, -1))
				{
					const char* fieldName = lua_tostring(L, -2);

					LuaExportedField field;
					field.name = String(fieldName);
					field.fieldProps = InferFieldPropsFromLua(L, -1);
					field.typeId = field.fieldProps.typeId;
					field.resolved = (field.typeId.id != 0);

					lua_pushvalue(L, -1);
					field.defaultValueRef = luaL_ref(L, LUA_REGISTRYINDEX);

					regType.fields.EmplaceBack(Traits::Move(field));
				}
				lua_pop(L, 1); // pop value, keep key
			}

			// Register/re-register with C++ reflection system
			RegisterLuaClass(L, typeIndex);

			if (isNew)
			{
				// Push TypeID as global so entity:GetComponent(Health) works
				lua_pushinteger(L, static_cast<lua_Integer>(typeId.id));
				lua_setglobal(L, name);

				luaRegistryLogger.Debug("Registered Lua component '{}' with {} fields", name, regType.fields.Size());
			}
			else if (App::ReloadedEnabled())
			{
				// Patch all live instances
				for (LuaComponentWrapper* wrapper : regType.liveInstances)
				{
					if (wrapper->GetLuaState() != L || wrapper->GetSelfRef() == LUA_NOREF)
					{
						continue;
					}

					wrapper->SetLuaState(L);

					// Update metatable __index to new class table
					lua_rawgeti(L, LUA_REGISTRYINDEX, wrapper->GetSelfRef());
					if (lua_getmetatable(L, -1))
					{
						lua_rawgeti(L, LUA_REGISTRYINDEX, regType.classTableRef);
						lua_setfield(L, -2, "__index");
						lua_pop(L, 1); // pop metatable
					}
					lua_pop(L, 1); // pop self table

					wrapper->ResetMethodRefs();
					wrapper->CacheAllMethods();
					wrapper->SetClassTableRef(regType.classTableRef);
				}

				luaRegistryLogger.Info("Lua component '{}' hot-reloaded ({} live instances)", name, regType.liveInstances.Size());
			}

			lua_pushinteger(L, static_cast<lua_Integer>(typeId.id));
			return 1;
		}

		// --- RegisterLuaClass ---

		void RegisterLuaClass(lua_State* L, usize typeIndex)
		{
			LuaRegisteredType& regType = registeredLuaTypes[typeIndex];

			// Build TypeProps
			TypeProps props{};
			props.typeId = regType.typeId;
			props.name = regType.name;
			props.isEnum = false;
			props.size = sizeof(LuaComponentWrapper);
			props.alignment = alignof(LuaComponentWrapper);

			// Register with reflection
			ReflectTypeBuilder builder = Reflection::RegisterType(regType.name, regType.name, props);
			builder.SetFnDestroy(LuaComponentWrapperDestroy);
			builder.SetFnDestructor(LuaComponentWrapperDestructor);

			// Constructor
			ReflectConstructorBuilder ctorBuilder = builder.AddConstructor(nullptr, nullptr, 0);
			ctorBuilder.SetPlacementNewFn(LuaComponentWrapperPlacementNew);
			ctorBuilder.SetNewObjectFn(LuaComponentWrapperNewObject);
			ctorBuilder.SetUserData(reinterpret_cast<VoidPtr>(regType.typeId.id));

			// Fields
			for (usize i = 0; i < regType.fields.Size(); ++i)
			{
				LuaExportedField& field = regType.fields[i];
				if (!field.resolved) continue;

				ReflectFieldBuilder fieldBuilder = builder.AddField(field.fieldProps, field.name);
				fieldBuilder.SetGet(LuaWrapperFieldGet);
				fieldBuilder.SetFnSet(LuaWrapperFieldSet);
				fieldBuilder.SetCopy(LuaWrapperFieldCopy);
				fieldBuilder.SetFnToResource(LuaFieldToResourceImpl);
				fieldBuilder.SetFnFromResource(LuaFieldFromResourceImpl);
				fieldBuilder.SetFnGetResourceFieldInfo(LuaFieldGetResourceFieldInfoImpl);
			}

			// Base type
			builder.AddBaseType(TypeInfo<Component>::ID());

			// Get final reflect type and build field access map
			regType.reflectType = Reflection::FindTypeById(regType.typeId);

			if (regType.reflectType)
			{
				Span<ReflectField*> fields = regType.reflectType->GetFields();
				usize reflectFieldIdx = 0;
				for (usize i = 0; i < regType.fields.Size() && reflectFieldIdx < fields.Size(); ++i)
				{
					if (!regType.fields[i].resolved) continue;

					LuaFieldAccessData accessData;
					accessData.fieldName = regType.fields[i].name;
					accessData.fieldIndex = i;
					accessData.registeredTypeIndex = typeIndex;
					luaFieldAccessDataMap.Insert(fields[reflectFieldIdx], accessData);
					++reflectFieldIdx;
				}
			}

			// Migrate resource system
			Resources::MigrateResourceForType(regType.typeId);
		}
	}

	// --- Public API ---

	void InitLuaTypeRegistry(lua_State* L)
	{
		// Clear state from previous init
		for (auto& regType : registeredLuaTypes)
		{
			if (L)
			{
				if (regType.classTableRef != LUA_NOREF)
				{
					luaL_unref(L, LUA_REGISTRYINDEX, regType.classTableRef);
				}
				for (auto& field : regType.fields)
				{
					if (field.defaultValueRef != LUA_NOREF)
					{
						luaL_unref(L, LUA_REGISTRYINDEX, field.defaultValueRef);
					}
				}
			}
		}
		registeredLuaTypes.Clear();
		typeIdToLuaTypeIndex.Clear();
		luaFieldAccessDataMap.Clear();

		// Register global function
		lua_pushcfunction(L, RegisterComponent_cfunc);
		lua_setglobal(L, "RegisterComponent");
	}

	bool IsLuaType(TypeID typeId)
	{
		auto it = typeIdToLuaTypeIndex.Find(typeId);
		return static_cast<bool>(it);
	}
}


// ============================================================================
// Section 3: Lua Script Engine
// ============================================================================

namespace Skore
{
	namespace
	{
		Logger& luaEngineLogger = Logger::GetLogger("Skore::LuaScriptEngine");

		bool luaInitialized = false;
		lua_State* globalLuaState = nullptr;

	}

	lua_State* GetLuaState()
	{
		return globalLuaState;
	}

	class LuaScriptEngine : public ScriptEngine
	{
	public:
		SK_CLASS(LuaScriptEngine, ScriptEngine);

		void Init() override
		{
			// Clean up previous state if re-initializing (e.g. after App::ResetContext)
			if (globalLuaState)
			{
				lua_close(globalLuaState);
				globalLuaState = nullptr;
			}

			// Clean up binding caches from previous init
			for (auto it = typeBindingCaches.begin(); it != typeBindingCaches.end(); ++it)
			{
				delete it->second;
			}
			typeBindingCaches.Clear();
			typeIdToMetatableName.Clear();

			globalLuaState = luaL_newstate();
			luaL_openlibs(globalLuaState);

			// Override print
			lua_pushcfunction(globalLuaState, LuaPrintOverride);
			lua_setglobal(globalLuaState, "print");

			InitLuaBindings(globalLuaState);
			InitLuaTypeRegistry(globalLuaState);

			luaInitialized = true;
			luaEngineLogger.Debug("Lua {} initialized", LUA_VERSION);
		}

		String GetExtension() override
		{
			return ".lua";
		}

		bool LoadScript(StringView source) override
		{

			if (source.Empty())
			{
				luaEngineLogger.Warn("lua source is empty");
				return false;
			}

			if (!luaInitialized || !globalLuaState)
			{
				luaEngineLogger.Error("Lua not initialized");
				return false;
			}

			int result = luaL_dostring(globalLuaState, source.CStr());
			if (result != LUA_OK)
			{
				const char* error = lua_tostring(globalLuaState, -1);
				if (error)
				{
					luaEngineLogger.Error("Lua error:\n{}", error);
				}
				lua_pop(globalLuaState, 1);
				return false;
			}

			return true;
		}

		bool LoadScript(Span<u8> bytecode) override
		{
			if (bytecode.Empty())
			{
				luaEngineLogger.Warn("lua bytecode is empty");
				return false;
			}

			if (!luaInitialized || !globalLuaState)
			{
				luaEngineLogger.Error("Lua not initialized");
				return false;
			}

			int result = luaL_loadbuffer(globalLuaState, reinterpret_cast<const char*>(bytecode.Data()), bytecode.Size(), "=bytecode");
			if (result != LUA_OK)
			{
				const char* error = lua_tostring(globalLuaState, -1);
				if (error)
				{
					luaEngineLogger.Error("Lua load error:\n{}", error);
				}
				lua_pop(globalLuaState, 1);
				return false;
			}

			result = lua_pcall(globalLuaState, 0, LUA_MULTRET, 0);
			if (result != LUA_OK)
			{
				const char* error = lua_tostring(globalLuaState, -1);
				if (error)
				{
					luaEngineLogger.Error("Lua error:\n{}", error);
				}
				lua_pop(globalLuaState, 1);
				return false;
			}

			return true;
		}

		ByteBuffer BuildScript(StringView source) override
		{
			ByteBuffer buffer;

			if (source.Empty())
			{
				luaEngineLogger.Warn("lua source is empty");
				return buffer;
			}

			if (!luaInitialized || !globalLuaState)
			{
				luaEngineLogger.Error("Lua not initialized");
				return buffer;
			}

			int result = luaL_loadstring(globalLuaState, source.CStr());
			if (result != LUA_OK)
			{
				const char* error = lua_tostring(globalLuaState, -1);
				if (error)
				{
					luaEngineLogger.Error("Lua compile error:\n{}", error);
				}
				lua_pop(globalLuaState, 1);
				return buffer;
			}

			lua_dump(globalLuaState, [](lua_State* L, const void* p, size_t sz, void* ud) -> int
			{
				ByteBuffer* buf = static_cast<ByteBuffer*>(ud);
				const u8* data = static_cast<const u8*>(p);
				buf->Insert(buf->end(), data, data + sz);
				return 0;
			}, &buffer, 0);

			lua_pop(globalLuaState, 1);
			return buffer;
		}

	};

	void RegisterLuaScriptEngine()
	{
		Reflection::Type<LuaScriptEngine>();
	}

	void UntrackLuaComponentInstance(LuaComponentWrapper* wrapper)
	{
		RemoveFromLiveInstances(wrapper);
	}
}


// ============================================================================
// Section 4: LuaComponentWrapper method implementations
// (placed here so LuaScriptEngine class is fully defined)
// ============================================================================

namespace Skore
{
	LuaComponentWrapper::~LuaComponentWrapper()
	{
		// Safety net: ensure this wrapper is removed from liveInstances no matter
		// which destruction path runs (Entity::DestroyComponent, reflection Destroy,
		// or direct DestroyAndFree). Double-remove is harmless.
		if (App::ReloadedEnabled())
		{
			UntrackLuaComponentInstance(this);
		}
	}

	void LuaComponentWrapper::Create(ComponentSettings& settings)
	{
		if (m_selfRef == LUA_NOREF) return;

		ExposeCppFieldsToLua();
		CacheAllMethods();

		settings.enableUpdate = (m_onUpdateRef != LUA_NOREF);
		settings.enableFixedUpdate = (m_onFixedUpdateRef != LUA_NOREF);
		settings.enableCollisionCallbacks = HasCollisionCallbacks();

		if (m_createRef != LUA_NOREF)
		{
			CallLuaMethod(m_createRef);
		}
	}

	void LuaComponentWrapper::Destroy()
	{
		if (m_selfRef != LUA_NOREF && m_destroyRef != LUA_NOREF)
		{
			CallLuaMethod(m_destroyRef);
		}

		// Remove from live instances tracking before refs are freed.
		// Entity::DestroyComponent calls Destroy() then DestroyAndFree() which
		// bypasses LuaComponentWrapperDestructor, so we must untrack here.
		if (App::ReloadedEnabled())
		{
			UntrackLuaComponentInstance(this);
		}

		UnrefAll();
	}

	void LuaComponentWrapper::OnStart()
	{
		if (m_selfRef != LUA_NOREF && m_onStartRef != LUA_NOREF)
		{
			CallLuaMethod(m_onStartRef);
		}
	}

	void LuaComponentWrapper::OnUpdate(f64 deltaTime)
	{
		if (m_selfRef != LUA_NOREF && m_onUpdateRef != LUA_NOREF)
		{
			CallLuaMethodWithDeltaTime(m_onUpdateRef, deltaTime);
		}
	}

	void LuaComponentWrapper::OnFixedUpdate()
	{
		if (m_selfRef != LUA_NOREF && m_onFixedUpdateRef != LUA_NOREF)
		{
			CallLuaMethod(m_onFixedUpdateRef);
		}
	}

	void LuaComponentWrapper::OnCollisionEnter(const Collision& collision)
	{
		if (m_selfRef != LUA_NOREF && m_onCollisionEnterRef != LUA_NOREF)
		{
			CallLuaMethodWithArg(m_onCollisionEnterRef, collision);
		}
	}

	void LuaComponentWrapper::OnCollisionStay(const Collision& collision)
	{
		if (m_selfRef != LUA_NOREF && m_onCollisionStayRef != LUA_NOREF)
		{
			CallLuaMethodWithArg(m_onCollisionStayRef, collision);
		}
	}

	void LuaComponentWrapper::OnCollisionExit(const Collision& collision)
	{
		if (m_selfRef != LUA_NOREF && m_onCollisionExitRef != LUA_NOREF)
		{
			CallLuaMethodWithArg(m_onCollisionExitRef, collision);
		}
	}

	void LuaComponentWrapper::OnTriggerEnter(const Collision& collision)
	{
		if (m_selfRef != LUA_NOREF && m_onTriggerEnterRef != LUA_NOREF)
		{
			CallLuaMethodWithArg(m_onTriggerEnterRef, collision);
		}
	}

	void LuaComponentWrapper::OnTriggerExit(const Collision& collision)
	{
		if (m_selfRef != LUA_NOREF && m_onTriggerExitRef != LUA_NOREF)
		{
			CallLuaMethodWithArg(m_onTriggerExitRef, collision);
		}
	}

	void LuaComponentWrapper::ProcessEvent(const EntityEventDesc& event)
	{
		if (m_selfRef != LUA_NOREF && m_processEventRef != LUA_NOREF)
		{
			CallLuaMethodWithArg(m_processEventRef, event);
		}
	}
}