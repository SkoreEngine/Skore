#pragma once
#include "Skore/Scene/Component.hpp"
#include "Skore/Script/LuaScriptEngine.hpp"

namespace Skore
{
	class LuaComponentWrapper : public Component
	{
	public:
		using Base = Component;

		~LuaComponentWrapper() override;

		TypeID GetTypeId() const override
		{
			return m_reflectTypeId.id != 0 ? m_reflectTypeId : TypeInfo<LuaComponentWrapper>::ID();
		}

		bool IsBaseOf(TypeID typeId) override
		{
			if (Component::IsBaseOf(typeId)) return true;
			if (m_reflectTypeId.id != 0 && typeId == m_reflectTypeId) return true;
			return typeId == TypeInfo<LuaComponentWrapper>::ID();
		}

		void SetLuaState(lua_State* L) { m_L = L; }
		lua_State* GetLuaState() const { return m_L; }

		void SetSelfRef(int ref) { m_selfRef = ref; }
		int GetSelfRef() const { return m_selfRef; }

		void SetClassTableRef(int ref) { m_classTableRef = ref; }
		int GetClassTableRef() const { return m_classTableRef; }

		void SetReflectTypeId(TypeID typeId) { m_reflectTypeId = typeId; }
		TypeID GetReflectTypeId() const { return m_reflectTypeId; }

		void CacheMethodRef(const char* name, int& outRef)
		{
			outRef = LUA_NOREF;
			if (!m_L || m_selfRef == LUA_NOREF) return;

			lua_rawgeti(m_L, LUA_REGISTRYINDEX, m_selfRef);
			if (!lua_istable(m_L, -1))
			{
				lua_pop(m_L, 1);
				return;
			}
			lua_getfield(m_L, -1, name);
			if (lua_isfunction(m_L, -1))
			{
				outRef = luaL_ref(m_L, LUA_REGISTRYINDEX);
			}
			else
			{
				lua_pop(m_L, 1);
			}
			lua_pop(m_L, 1); // pop self table
		}

		void FreeMethodRef(int& ref)
		{
			if (m_L && ref != LUA_NOREF)
			{
				luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
				ref = LUA_NOREF;
			}
		}

		void ResetMethodRefs()
		{
			m_createRef = LUA_NOREF;
			m_destroyRef = LUA_NOREF;
			m_onStartRef = LUA_NOREF;
			m_onUpdateRef = LUA_NOREF;
			m_onFixedUpdateRef = LUA_NOREF;
			m_onCollisionEnterRef = LUA_NOREF;
			m_onCollisionStayRef = LUA_NOREF;
			m_onCollisionExitRef = LUA_NOREF;
			m_onTriggerEnterRef = LUA_NOREF;
			m_onTriggerExitRef = LUA_NOREF;
			m_processEventRef = LUA_NOREF;
		}

		void CacheAllMethods()
		{
			// Free existing method refs before re-caching
			FreeMethodRef(m_createRef);
			FreeMethodRef(m_destroyRef);
			FreeMethodRef(m_onStartRef);
			FreeMethodRef(m_onUpdateRef);
			FreeMethodRef(m_onFixedUpdateRef);
			FreeMethodRef(m_onCollisionEnterRef);
			FreeMethodRef(m_onCollisionStayRef);
			FreeMethodRef(m_onCollisionExitRef);
			FreeMethodRef(m_onTriggerEnterRef);
			FreeMethodRef(m_onTriggerExitRef);
			FreeMethodRef(m_processEventRef);

			CacheMethodRef("Create", m_createRef);
			CacheMethodRef("Destroy", m_destroyRef);
			CacheMethodRef("OnStart", m_onStartRef);
			CacheMethodRef("OnUpdate", m_onUpdateRef);
			CacheMethodRef("OnFixedUpdate", m_onFixedUpdateRef);
			CacheMethodRef("OnCollisionEnter", m_onCollisionEnterRef);
			CacheMethodRef("OnCollisionStay", m_onCollisionStayRef);
			CacheMethodRef("OnCollisionExit", m_onCollisionExitRef);
			CacheMethodRef("OnTriggerEnter", m_onTriggerEnterRef);
			CacheMethodRef("OnTriggerExit", m_onTriggerExitRef);
			CacheMethodRef("ProcessEvent", m_processEventRef);
		}

		void ExposeCppFieldsToLua()
		{
			if (!m_L || m_selfRef == LUA_NOREF) return;

			lua_rawgeti(m_L, LUA_REGISTRYINDEX, m_selfRef);
			if (!lua_istable(m_L, -1))
			{
				lua_pop(m_L, 1);
				return;
			}

			if (entity)
			{
				FieldProps props{};
				props.typeId = TypeInfo<Entity>::ID();
				props.isPointer = true;
				LuaCppToLua(m_L, &entity, props);
				lua_setfield(m_L, -2, "entity");
			}

			if (scene)
			{
				FieldProps props{};
				props.typeId = TypeInfo<Scene>::ID();
				props.isPointer = true;
				LuaCppToLua(m_L, &scene, props);
				lua_setfield(m_L, -2, "scene");
			}

			lua_pop(m_L, 1); // pop self table
		}

		void Create(ComponentSettings& settings) override;
		void Destroy() override;
		void OnStart() override;
		void OnUpdate(f64 deltaTime) override;
		void OnFixedUpdate() override;
		void OnCollisionEnter(const Collision& collision) override;
		void OnCollisionStay(const Collision& collision) override;
		void OnCollisionExit(const Collision& collision) override;
		void OnTriggerEnter(const Collision& collision) override;
		void OnTriggerExit(const Collision& collision) override;
		void ProcessEvent(const EntityEventDesc& event) override;

	private:
		bool HasCollisionCallbacks() const
		{
			return m_onCollisionEnterRef != LUA_NOREF ||
			       m_onCollisionStayRef != LUA_NOREF ||
			       m_onCollisionExitRef != LUA_NOREF ||
			       m_onTriggerEnterRef != LUA_NOREF ||
			       m_onTriggerExitRef != LUA_NOREF;
		}

		void CallLuaMethod(int funcRef)
		{
			if (!m_L || funcRef == LUA_NOREF || m_selfRef == LUA_NOREF) return;

			lua_pushcfunction(m_L, LuaErrorHandler);
			int errIdx = lua_gettop(m_L);

			lua_rawgeti(m_L, LUA_REGISTRYINDEX, funcRef);
			lua_rawgeti(m_L, LUA_REGISTRYINDEX, m_selfRef);

			if (lua_pcall(m_L, 1, 0, errIdx) != LUA_OK)
			{
				lua_pop(m_L, 1); // pop error
			}
			lua_pop(m_L, 1); // pop error handler
		}

		template <typename T>
		void CallLuaMethodWithArg(int funcRef, const T& arg)
		{
			if (!m_L || funcRef == LUA_NOREF || m_selfRef == LUA_NOREF) return;

			lua_pushcfunction(m_L, LuaErrorHandler);
			int errIdx = lua_gettop(m_L);

			lua_rawgeti(m_L, LUA_REGISTRYINDEX, funcRef);
			lua_rawgeti(m_L, LUA_REGISTRYINDEX, m_selfRef);
			LuaCppToLua(m_L, arg);

			if (lua_pcall(m_L, 2, 0, errIdx) != LUA_OK)
			{
				lua_pop(m_L, 1); // pop error
			}
			lua_pop(m_L, 1); // pop error handler
		}

		void CallLuaMethodWithDeltaTime(int funcRef, f64 deltaTime)
		{
			if (!m_L || funcRef == LUA_NOREF || m_selfRef == LUA_NOREF) return;

			lua_pushcfunction(m_L, LuaErrorHandler);
			int errIdx = lua_gettop(m_L);

			lua_rawgeti(m_L, LUA_REGISTRYINDEX, funcRef);
			lua_rawgeti(m_L, LUA_REGISTRYINDEX, m_selfRef);
			lua_pushnumber(m_L, static_cast<lua_Number>(deltaTime));

			if (lua_pcall(m_L, 2, 0, errIdx) != LUA_OK)
			{
				lua_pop(m_L, 1); // pop error
			}
			lua_pop(m_L, 1); // pop error handler
		}

		void UnrefAll()
		{
			if (!m_L) return;
			auto unref = [this](int& ref)
			{
				if (ref != LUA_NOREF)
				{
					luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
					ref = LUA_NOREF;
				}
			};
			unref(m_selfRef);
			// NOTE: m_classTableRef is shared across all instances of this Lua type
			// (it's the LuaRegisteredType::classTableRef). Do NOT unref it here —
			// it's owned by the type registry and cleaned up in InitLuaTypeRegistry.
			m_classTableRef = LUA_NOREF;
			unref(m_createRef);
			unref(m_destroyRef);
			unref(m_onStartRef);
			unref(m_onUpdateRef);
			unref(m_onFixedUpdateRef);
			unref(m_onCollisionEnterRef);
			unref(m_onCollisionStayRef);
			unref(m_onCollisionExitRef);
			unref(m_onTriggerEnterRef);
			unref(m_onTriggerExitRef);
			unref(m_processEventRef);
		}

		static int LuaErrorHandler(lua_State* L)
		{
			const char* msg = lua_tostring(L, 1);
			luaL_traceback(L, L, msg, 1);
			return 1;
		}

		lua_State* m_L = nullptr;
		int m_selfRef = LUA_NOREF;
		int m_classTableRef = LUA_NOREF;
		TypeID m_reflectTypeId{};

		int m_createRef = LUA_NOREF;
		int m_destroyRef = LUA_NOREF;
		int m_onStartRef = LUA_NOREF;
		int m_onUpdateRef = LUA_NOREF;
		int m_onFixedUpdateRef = LUA_NOREF;
		int m_onCollisionEnterRef = LUA_NOREF;
		int m_onCollisionStayRef = LUA_NOREF;
		int m_onCollisionExitRef = LUA_NOREF;
		int m_onTriggerEnterRef = LUA_NOREF;
		int m_onTriggerExitRef = LUA_NOREF;
		int m_processEventRef = LUA_NOREF;
	};
}
