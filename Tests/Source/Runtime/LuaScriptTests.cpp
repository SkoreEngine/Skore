#include "doctest.h"
#include "Skore/App.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Script/ScriptManager.hpp"
#include "Skore/Scene/Scene.hpp"
#include "Skore/Scene/Entity.hpp"
#include "Skore/Scene/Component.hpp"

using namespace Skore;

namespace
{
	struct ScriptTestVec3
	{
		f32 x = 0.0f;
		f32 y = 0.0f;
		f32 z = 0.0f;

		ScriptTestVec3() = default;
		ScriptTestVec3(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}

		f32 Length() const
		{
			return x * x + y * y + z * z;
		}

		f32 Dot(const ScriptTestVec3& other) const
		{
			return x * other.x + y * other.y + z * other.z;
		}

		void Scale(f32 factor)
		{
			x *= factor;
			y *= factor;
			z *= factor;
		}

		void Set(f32 newX, f32 newY, f32 newZ)
		{
			x = newX;
			y = newY;
			z = newZ;
		}

		static ScriptTestVec3 Zero()
		{
			return ScriptTestVec3(0.0f, 0.0f, 0.0f);
		}

		static ScriptTestVec3 One()
		{
			return ScriptTestVec3(1.0f, 1.0f, 1.0f);
		}

		static f32 Distance(const ScriptTestVec3& a, const ScriptTestVec3& b)
		{
			f32 dx = a.x - b.x;
			f32 dy = a.y - b.y;
			f32 dz = a.z - b.z;
			return dx * dx + dy * dy + dz * dz;
		}

		static void RegisterType(NativeReflectType<ScriptTestVec3>& type)
		{
			type.Constructor<f32, f32, f32>("x", "y", "z");
			type.Field<&ScriptTestVec3::x>("x");
			type.Field<&ScriptTestVec3::y>("y");
			type.Field<&ScriptTestVec3::z>("z");
			type.Function<&ScriptTestVec3::Length>("Length");
			type.Function<&ScriptTestVec3::Dot>("Dot", "other");
			type.Function<&ScriptTestVec3::Scale>("Scale", "factor");
			type.Function<&ScriptTestVec3::Set>("Set", "newX", "newY", "newZ");
			type.Function<&ScriptTestVec3::Zero>("Zero");
			type.Function<&ScriptTestVec3::One>("One");
			type.Function<&ScriptTestVec3::Distance>("Distance", "a", "b");
		}
	};

	struct ScriptTestData
	{
		i32    intValue = 0;
		f32    floatValue = 0.0f;
		bool   boolValue = false;
		String stringValue;

		ScriptTestData() = default;

		void SetInt(i32 val) { intValue = val; }
		i32 GetInt() const { return intValue; }

		void SetFloat(f32 val) { floatValue = val; }
		f32 GetFloat() const { return floatValue; }

		void SetBool(bool val) { boolValue = val; }
		bool GetBool() const { return boolValue; }

		void SetString(const String& val) { stringValue = val; }
		String GetString() const { return stringValue; }

		i32 Add(i32 a) { return intValue + a; }
		i32 Add(i32 a, i32 b) { return intValue + a + b; }

		static void RegisterType(NativeReflectType<ScriptTestData>& type)
		{
			type.Field<&ScriptTestData::intValue>("intValue");
			type.Field<&ScriptTestData::floatValue>("floatValue");
			type.Field<&ScriptTestData::boolValue>("boolValue");
			type.Field<&ScriptTestData::stringValue>("stringValue");
			type.Function<&ScriptTestData::SetInt>("SetInt", "val");
			type.Function<&ScriptTestData::GetInt>("GetInt");
			type.Function<&ScriptTestData::SetFloat>("SetFloat", "val");
			type.Function<&ScriptTestData::GetFloat>("GetFloat");
			type.Function<&ScriptTestData::SetBool>("SetBool", "val");
			type.Function<&ScriptTestData::GetBool>("GetBool");
			type.Function<&ScriptTestData::SetString>("SetString", "val");
			type.Function<&ScriptTestData::GetString>("GetString");
			type.Function<static_cast<i32(ScriptTestData::*)(i32)>(&ScriptTestData::Add)>("Add", "a");
			type.Function<static_cast<i32(ScriptTestData::*)(i32, i32)>(&ScriptTestData::Add)>("Add", "a", "b");
		}
	};

	enum class ScriptTestColor
	{
		Red = 0,
		Green = 1,
		Blue = 2,
		Yellow = 10
	};

	struct ScriptTestWithEnum
	{
		ScriptTestColor color = ScriptTestColor::Red;

		ScriptTestWithEnum() = default;

		void SetColor(ScriptTestColor c) { color = c; }
		ScriptTestColor GetColor() const { return color; }

		static void RegisterType(NativeReflectType<ScriptTestWithEnum>& type)
		{
			type.Field<&ScriptTestWithEnum::color>("color");
			type.Function<&ScriptTestWithEnum::SetColor>("SetColor", "c");
			type.Function<&ScriptTestWithEnum::GetColor>("GetColor");
		}
	};

	struct ScriptTestContainer
	{
		Array<i32> intArray;
		Array<f32> floatArray;
		HashMap<String, i32> stringIntMap;

		ScriptTestContainer() = default;

		Array<i32> GetIntArray() const { return intArray; }
		void SetIntArray(const Array<i32>& arr) { intArray = arr; }

		Span<i32> GetIntSpan() { return Span<i32>(intArray); }

		Array<f32> GetFloatArray() const { return floatArray; }
		void SetFloatArray(const Array<f32>& arr) { floatArray = arr; }

		HashMap<String, i32> GetStringIntMap() const { return stringIntMap; }
		void SetStringIntMap(const HashMap<String, i32>& m) { stringIntMap = m; }

		i32 SumIntArray() const
		{
			i32 sum = 0;
			for (usize i = 0; i < intArray.Size(); i++)
			{
				sum += intArray[i];
			}
			return sum;
		}

		static Array<i32> MakeRange(i32 count)
		{
			Array<i32> result;
			for (i32 i = 0; i < count; i++)
			{
				result.EmplaceBack(i);
			}
			return result;
		}

		static HashMap<String, i32> MakeSampleMap()
		{
			HashMap<String, i32> result;
			result.Insert("alpha", 1);
			result.Insert("beta", 2);
			result.Insert("gamma", 3);
			return result;
		}

		static void RegisterType(NativeReflectType<ScriptTestContainer>& type)
		{
			type.Field<&ScriptTestContainer::intArray>("intArray");
			type.Field<&ScriptTestContainer::floatArray>("floatArray");
			type.Field<&ScriptTestContainer::stringIntMap>("stringIntMap");
			type.Function<&ScriptTestContainer::GetIntArray>("GetIntArray");
			type.Function<&ScriptTestContainer::SetIntArray>("SetIntArray", "arr");
			type.Function<&ScriptTestContainer::GetIntSpan>("GetIntSpan");
			type.Function<&ScriptTestContainer::GetFloatArray>("GetFloatArray");
			type.Function<&ScriptTestContainer::SetFloatArray>("SetFloatArray", "arr");
			type.Function<&ScriptTestContainer::GetStringIntMap>("GetStringIntMap");
			type.Function<&ScriptTestContainer::SetStringIntMap>("SetStringIntMap", "m");
			type.Function<&ScriptTestContainer::SumIntArray>("SumIntArray");
			type.Function<&ScriptTestContainer::MakeRange>("MakeRange", "count");
			type.Function<&ScriptTestContainer::MakeSampleMap>("MakeSampleMap");
		}
	};

	bool luaScriptEngineInitialized = false;

	void InitLuaScriptTestTypes()
	{
		if (!luaScriptEngineInitialized)
		{
			App::ResetContext();
			Reflection::Type<ScriptTestVec3>();
			Reflection::Type<ScriptTestData>();

			auto colorEnum = Reflection::Type<ScriptTestColor>();
			colorEnum.Value<ScriptTestColor::Red>("Red");
			colorEnum.Value<ScriptTestColor::Green>("Green");
			colorEnum.Value<ScriptTestColor::Blue>("Blue");
			colorEnum.Value<ScriptTestColor::Yellow>("Yellow");

			Reflection::Type<ScriptTestWithEnum>();
			Reflection::Type<ScriptTestContainer>();

			ScriptManager::Init();
			luaScriptEngineInitialized = true;
		}
	}

	// -----------------------------------------------------------------------
	// Basic Execution
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::BasicLuaExecution")
	{
		InitLuaScriptTestTypes();
		CHECK(ScriptManager::LoadScript("print('Hello from Lua test!')", ".lua"));
	}

	// -----------------------------------------------------------------------
	// Instance Creation
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::CreateInstance")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local v = ScriptTestVec3(1.0, 2.0, 3.0)
print("Created vector:", v.x, v.y, v.z)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Field Access
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::AccessFields")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local v = ScriptTestVec3(1.0, 2.0, 3.0)
local total = v.x + v.y + v.z
print("Sum of fields:", total)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::SetFields")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local v = ScriptTestVec3(1.0, 2.0, 3.0)
print("Before:", v.x)
v.x = 10.0
print("After:", v.x)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Method Calls
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::CallMethodNoArgs")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local v = ScriptTestVec3(3.0, 4.0, 0.0)
local length = v:Length()
print("Length squared:", length)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::CallMethodWithArgs")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local v = ScriptTestVec3(1.0, 2.0, 3.0)
v:Scale(2.0)
print("After Scale(2.0):", v.x, v.y, v.z)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::CallMethodMultipleArgs")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local v = ScriptTestVec3(0.0, 0.0, 0.0)
v:Set(5.0, 10.0, 15.0)
print("After Set:", v.x, v.y, v.z)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Primitive Types
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::IntegerTypes")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local d = ScriptTestData()
d.intValue = 42
print("Integer value:", d.intValue)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::BooleanTypes")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local d = ScriptTestData()
d.boolValue = true
print("Boolean value:", d.boolValue)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::StringTypes")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local d = ScriptTestData()
d.stringValue = "Hello Skore"
print("String value:", d.stringValue)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Return Values
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::MethodWithReturnValue")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local d = ScriptTestData()
d.intValue = 10
local result = d:GetInt()
print("GetInt result:", result)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Overloaded Methods
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::OverloadedMethodSingleArg")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local d = ScriptTestData()
d.intValue = 10
local result = d:Add(5)
print("Add(5) result:", result)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::OverloadedMethodTwoArgs")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local d = ScriptTestData()
d.intValue = 10
local result = d:Add(5, 3)
print("Add(5, 3) result:", result)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Constructors
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::DefaultConstructor")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local d = ScriptTestData()
print("Default intValue:", d.intValue)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Multiple Instances
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::MultipleInstances")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local v1 = ScriptTestVec3(1.0, 0.0, 0.0)
local v2 = ScriptTestVec3(0.0, 2.0, 0.0)
local total = v1.x + v2.y
print("Multiple instances total:", total)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Static Methods
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::StaticMethodNoArgs")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local v = ScriptTestVec3.Zero()
print("Zero vector:", v.x, v.y, v.z)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::StaticMethodWithArgs")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local a = ScriptTestVec3(0.0, 0.0, 0.0)
local b = ScriptTestVec3(3.0, 4.0, 0.0)
local dist = ScriptTestVec3.Distance(a, b)
print("Distance squared:", dist)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Enum Types
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::EnumValues")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
print("Red:", ScriptTestColor.Red)
print("Green:", ScriptTestColor.Green)
print("Blue:", ScriptTestColor.Blue)
print("Yellow:", ScriptTestColor.Yellow)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::EnumComparison")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
print("Red == 0:", ScriptTestColor.Red == 0)
print("Green == 1:", ScriptTestColor.Green == 1)
print("Yellow == 10:", ScriptTestColor.Yellow == 10)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::EnumFieldAccess")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local obj = ScriptTestWithEnum()
print("Default color:", obj.color)
print("Is Red:", obj.color == ScriptTestColor.Red)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::EnumFieldSet")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local obj = ScriptTestWithEnum()
obj.color = ScriptTestColor.Blue
print("After set to Blue:", obj.color)
print("Is Blue:", obj.color == ScriptTestColor.Blue)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::EnumMethodParam")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local obj = ScriptTestWithEnum()
obj:SetColor(ScriptTestColor.Green)
local result = obj:GetColor()
print("After SetColor(Green):", result)
print("Is Green:", result == ScriptTestColor.Green)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Error Handling
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::NonExistentMethodError")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local v = ScriptTestVec3(1.0, 2.0, 3.0)
v:NonExistentMethod()
)";
		// Should fail because NonExistentMethod returns nil, calling nil errors
		CHECK_FALSE(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::NonExistentFieldError")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local v = ScriptTestVec3(1.0, 2.0, 3.0)
-- Accessing a non-existent field returns nil, which is not an error in Lua
-- But we can test that it IS nil
assert(v.nonExistentField == nil, "Expected nil for non-existent field")
print("Non-existent field is nil as expected")
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Array<T> Conversion
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::ArrayToTable::StaticMethodReturnsArray")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local arr = ScriptTestContainer.MakeRange(5)
assert(type(arr) == "table", "Expected table, got " .. type(arr))
assert(#arr == 5, "Expected 5 elements, got " .. #arr)
assert(arr[1] == 0, "arr[1] should be 0, got " .. tostring(arr[1]))
assert(arr[2] == 1, "arr[2] should be 1, got " .. tostring(arr[2]))
assert(arr[5] == 4, "arr[5] should be 4, got " .. tostring(arr[5]))
print("MakeRange(5):", arr[1], arr[2], arr[3], arr[4], arr[5])
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::TableToArray::MethodAcceptsArray")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local c = ScriptTestContainer()
c:SetIntArray({10, 20, 30, 40, 50})
local arr = c:GetIntArray()
assert(#arr == 5, "Expected 5 elements, got " .. #arr)
assert(arr[1] == 10)
assert(arr[3] == 30)
assert(arr[5] == 50)
print("SetIntArray/GetIntArray roundtrip OK")
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::ArrayField::SumIntArray")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local c = ScriptTestContainer()
c:SetIntArray({1, 2, 3, 4, 5})
local sum = c:SumIntArray()
assert(sum == 15, "Expected sum 15, got " .. tostring(sum))
print("SumIntArray:", sum)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::ArrayToTable::FloatArray")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local c = ScriptTestContainer()
c:SetFloatArray({1.5, 2.5, 3.5})
local arr = c:GetFloatArray()
assert(#arr == 3, "Expected 3 elements")
assert(arr[1] > 1.4 and arr[1] < 1.6, "arr[1] should be ~1.5")
assert(arr[2] > 2.4 and arr[2] < 2.6, "arr[2] should be ~2.5")
assert(arr[3] > 3.4 and arr[3] < 3.6, "arr[3] should be ~3.5")
print("Float array roundtrip OK:", arr[1], arr[2], arr[3])
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::ArrayToTable::EmptyArray")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local arr = ScriptTestContainer.MakeRange(0)
assert(type(arr) == "table", "Expected table")
assert(#arr == 0, "Expected empty table, got " .. #arr)
print("Empty array OK")
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// Span<T> Conversion
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::SpanToTable::MethodReturnsSpan")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local c = ScriptTestContainer()
c:SetIntArray({10, 20, 30, 40, 50})
local span = c:GetIntSpan()
assert(type(span) == "table", "Expected table, got " .. type(span))
assert(#span == 5, "Expected 5 elements, got " .. #span)
assert(span[1] == 10, "span[1] should be 10, got " .. tostring(span[1]))
assert(span[3] == 30, "span[3] should be 30, got " .. tostring(span[3]))
assert(span[5] == 50, "span[5] should be 50, got " .. tostring(span[5]))
print("Span roundtrip OK:", span[1], span[2], span[3], span[4], span[5])
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// HashMap<K,V> Conversion
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::HashMapToTable::StaticMethodReturnsMap")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local m = ScriptTestContainer.MakeSampleMap()
assert(type(m) == "table", "Expected table, got " .. type(m))
assert(m.alpha == 1, "alpha should be 1, got " .. tostring(m.alpha))
assert(m.beta == 2, "beta should be 2, got " .. tostring(m.beta))
assert(m.gamma == 3, "gamma should be 3, got " .. tostring(m.gamma))
print("MakeSampleMap:", m.alpha, m.beta, m.gamma)
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	TEST_CASE("LuaScript::TableToHashMap::MethodAcceptsMap")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
local c = ScriptTestContainer()
c:SetStringIntMap({x = 100, y = 200, z = 300})
local m = c:GetStringIntMap()
assert(m.x == 100, "x should be 100, got " .. tostring(m.x))
assert(m.y == 200, "y should be 200, got " .. tostring(m.y))
assert(m.z == 300, "z should be 300, got " .. tostring(m.z))
print("SetStringIntMap/GetStringIntMap roundtrip OK")
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));
	}

	// -----------------------------------------------------------------------
	// RegisterComponent: Basic Registration
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::RegisterComponent::BasicRegistration")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
RegisterComponent("LuaHealth", {
    hp = 100,
    maxHp = 200,

    OnStart = function(self)
        print("LuaHealth OnStart")
    end,

    OnUpdate = function(self, dt)
        -- nothing
    end
})
print("LuaHealth registered")
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaHealth");
		REQUIRE(reflType != nullptr);
		CHECK(reflType->GetProps().typeId.id != 0);
		CHECK(reflType->IsDerivedOf(TypeInfo<Component>::ID()));
	}

	// -----------------------------------------------------------------------
	// RegisterComponent: Field Type Inference
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::RegisterComponent::FieldTypeInference")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
RegisterComponent("LuaFieldTest", {
    intField = 42,
    floatField = 3.14,
    boolField = true,
    stringField = "hello"
})
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaFieldTest");
		REQUIRE(reflType != nullptr);

		// Check that fields were created
		Span<ReflectField*> fields = reflType->GetFields();
		CHECK(fields.Size() >= 4);

		// Verify specific fields exist
		CHECK(reflType->FindField("intField") != nullptr);
		CHECK(reflType->FindField("floatField") != nullptr);
		CHECK(reflType->FindField("boolField") != nullptr);
		CHECK(reflType->FindField("stringField") != nullptr);
	}

	// -----------------------------------------------------------------------
	// RegisterComponent: Create Instance via Reflection
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::LuaComponent::CreateInstance")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
RegisterComponent("LuaCreatable", {
    value = 10,
    name = "default"
})
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaCreatable");
		REQUIRE(reflType != nullptr);

		Object* obj = reflType->NewObject();
		REQUIRE(obj != nullptr);

		// Verify default field values via reflection
		ReflectField* valueField = reflType->FindField("value");
		REQUIRE(valueField != nullptr);

		i32 value = 0;
		valueField->Get(obj, &value, sizeof(value));
		CHECK(value == 10);

		reflType->Destroy(obj);
	}

	// -----------------------------------------------------------------------
	// RegisterComponent: Get/Set Field via Reflection
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::LuaComponent::GetSetField")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
RegisterComponent("LuaGetSet", {
    score = 0
})
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaGetSet");
		REQUIRE(reflType != nullptr);

		Object* obj = reflType->NewObject();
		REQUIRE(obj != nullptr);

		ReflectField* scoreField = reflType->FindField("score");
		REQUIRE(scoreField != nullptr);

		// Read default
		i32 score = 0;
		scoreField->Get(obj, &score, sizeof(score));
		CHECK(score == 0);

		// Write new value
		i32 newScore = 42;
		scoreField->Set(obj, &newScore, sizeof(newScore));

		// Read back
		i32 readBack = 0;
		scoreField->Get(obj, &readBack, sizeof(readBack));
		CHECK(readBack == 42);

		reflType->Destroy(obj);
	}

	// -----------------------------------------------------------------------
	// LuaComponent: OnStart Called
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::LuaComponent::OnStartCalled")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
RegisterComponent("LuaStartComp", {
    started = false,

    OnStart = function(self)
        self.started = true
    end
})
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaStartComp");
		REQUIRE(reflType != nullptr);

		Scene scene;
		Entity* entity = scene.CreateEntity();
		REQUIRE(entity != nullptr);

		Component* comp = entity->AddComponent(reflType);
		REQUIRE(comp != nullptr);

		// Directly call OnStart
		comp->OnStart();

		ReflectField* startedField = reflType->FindField("started");
		REQUIRE(startedField != nullptr);

		bool started = false;
		startedField->Get(comp, &started, sizeof(started));
		CHECK(started == true);
	}

	// -----------------------------------------------------------------------
	// LuaComponent: OnUpdate Called
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::LuaComponent::OnUpdateCalled")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
RegisterComponent("LuaTickComp", {
    ticks = 0,

    OnUpdate = function(self, dt)
        self.ticks = self.ticks + 1
    end
})
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaTickComp");
		REQUIRE(reflType != nullptr);

		Scene scene;
		Entity* entity = scene.CreateEntity();
		REQUIRE(entity != nullptr);

		Component* comp = entity->AddComponent(reflType);
		REQUIRE(comp != nullptr);

		// Directly call OnUpdate
		comp->OnUpdate(0.016);
		comp->OnUpdate(0.016);
		comp->OnUpdate(0.016);

		ReflectField* ticksField = reflType->FindField("ticks");
		REQUIRE(ticksField != nullptr);

		i32 ticks = 0;
		ticksField->Get(comp, &ticks, sizeof(ticks));
		CHECK(ticks == 3);
	}

	// -----------------------------------------------------------------------
	// LuaComponent: Self Field Access in Callbacks
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::LuaComponent::SelfFieldAccess")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
RegisterComponent("LuaSelfAccess", {
    hp = 100,
    damaged = false,

    OnUpdate = function(self, dt)
        self.hp = self.hp - 10
        if self.hp < 100 then
            self.damaged = true
        end
    end
})
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaSelfAccess");
		REQUIRE(reflType != nullptr);

		Scene scene;
		Entity* entity = scene.CreateEntity();
		Component* comp = entity->AddComponent(reflType);
		REQUIRE(comp != nullptr);

		comp->OnUpdate(0.016);

		ReflectField* hpField = reflType->FindField("hp");
		ReflectField* damagedField = reflType->FindField("damaged");
		REQUIRE(hpField != nullptr);
		REQUIRE(damagedField != nullptr);

		i32 hp = 0;
		hpField->Get(comp, &hp, sizeof(hp));
		CHECK(hp == 90);

		bool damaged = false;
		damagedField->Get(comp, &damaged, sizeof(damaged));
		CHECK(damaged == true);
	}

	// -----------------------------------------------------------------------
	// LuaComponent: Entity Access
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::LuaComponent::EntityAccess")
	{
		InitLuaScriptTestTypes();

		const char* script = R"(
RegisterComponent("LuaEntityAccess", {
    hasEntity = false,

    OnUpdate = function(self, dt)
        if self.entity then
            self.hasEntity = true
        end
    end
})
)";
		CHECK(ScriptManager::LoadScript(script, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaEntityAccess");
		REQUIRE(reflType != nullptr);

		Scene scene;
		Entity* entity = scene.CreateEntity();
		Component* comp = entity->AddComponent(reflType);
		REQUIRE(comp != nullptr);

		comp->OnUpdate(0.016);

		ReflectField* hasEntityField = reflType->FindField("hasEntity");
		REQUIRE(hasEntityField != nullptr);

		bool hasEntity = false;
		hasEntityField->Get(comp, &hasEntity, sizeof(hasEntity));
		CHECK(hasEntity == true);
	}

	// -----------------------------------------------------------------------
	// LuaComponent: GetComponent from Lua
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::LuaComponent::GetComponent")
	{
		InitLuaScriptTestTypes();

		// Register a Health component with an hp field
		const char* healthScript = R"(
RegisterComponent("LuaHealthTarget", {
    hp = 100
})
)";
		CHECK(ScriptManager::LoadScript(healthScript, ".lua"));

		// Register a Reader component that fetches Health via GetComponent
		const char* readerScript = R"(
RegisterComponent("LuaHealthReader", {
    foundHealth = false,
    readHp = 0,

    OnStart = function(self)
        local health = self.entity:GetComponent(LuaHealthTarget)
        if health then
            self.foundHealth = true
            self.readHp = health.hp
        end
    end
})
)";
		CHECK(ScriptManager::LoadScript(readerScript, ".lua"));

		ReflectType* healthType = Reflection::FindTypeByName("LuaHealthTarget");
		ReflectType* readerType = Reflection::FindTypeByName("LuaHealthReader");
		REQUIRE(healthType != nullptr);
		REQUIRE(readerType != nullptr);

		Scene scene;
		Entity* entity = scene.CreateEntity();

		// Add health component first
		Component* healthComp = entity->AddComponent(healthType);
		REQUIRE(healthComp != nullptr);

		// Add reader component second
		Component* readerComp = entity->AddComponent(readerType);
		REQUIRE(readerComp != nullptr);

		// Call OnStart on reader, which should find the health component
		readerComp->OnStart();

		ReflectField* foundField = readerType->FindField("foundHealth");
		REQUIRE(foundField != nullptr);
		bool foundHealth = false;
		foundField->Get(readerComp, &foundHealth, sizeof(foundHealth));
		CHECK(foundHealth == true);

		ReflectField* readHpField = readerType->FindField("readHp");
		REQUIRE(readHpField != nullptr);
		i32 readHp = 0;
		readHpField->Get(readerComp, &readHp, sizeof(readHp));
		CHECK(readHp == 100);
	}

	// -----------------------------------------------------------------------
	// Hot Reload: Re-registering a component updates live instances
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::HotReload::MethodUpdate")
	{
		InitLuaScriptTestTypes();
		App::SetReloadEnabled(true);

		// Initial script: OnUpdate sets value = 1
		const char* scriptV1 = R"(
RegisterComponent("LuaHotReloadComp", {
    value = 0,

    OnUpdate = function(self, dt)
        self.value = 1
    end
})
)";
		CHECK(ScriptManager::LoadScript(scriptV1, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaHotReloadComp");
		REQUIRE(reflType != nullptr);

		Scene scene;
		Entity* entity = scene.CreateEntity();
		REQUIRE(entity != nullptr);

		Component* comp = entity->AddComponent(reflType);
		REQUIRE(comp != nullptr);

		// Run OnUpdate — should set value = 1
		comp->OnUpdate(0.016);

		ReflectField* valueField = reflType->FindField("value");
		REQUIRE(valueField != nullptr);

		i32 value = 0;
		valueField->Get(comp, &value, sizeof(value));
		CHECK(value == 1);

		// Hot reload: OnUpdate now sets value = 2
		const char* scriptV2 = R"(
RegisterComponent("LuaHotReloadComp", {
    value = 0,

    OnUpdate = function(self, dt)
        self.value = 2
    end
})
)";
		CHECK(ScriptManager::LoadScript(scriptV2, ".lua"));

		// Run OnUpdate again — should use updated method, setting value = 2
		comp->OnUpdate(0.016);

		i32 newValue = 0;
		valueField->Get(comp, &newValue, sizeof(newValue));
		CHECK(newValue == 2);

		App::SetReloadEnabled(false);
	}

	TEST_CASE("LuaScript::HotReload::AddMethod")
	{
		InitLuaScriptTestTypes();
		App::SetReloadEnabled(true);

		// Initial script: no OnUpdate
		const char* scriptV1 = R"(
RegisterComponent("LuaHotReloadAdd", {
    value = 0,

    OnStart = function(self)
        self.value = 10
    end
})
)";
		CHECK(ScriptManager::LoadScript(scriptV1, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaHotReloadAdd");
		REQUIRE(reflType != nullptr);

		Scene scene;
		Entity* entity = scene.CreateEntity();
		REQUIRE(entity != nullptr);

		Component* comp = entity->AddComponent(reflType);
		REQUIRE(comp != nullptr);

		comp->OnStart();

		ReflectField* valueField = reflType->FindField("value");
		REQUIRE(valueField != nullptr);

		i32 value = 0;
		valueField->Get(comp, &value, sizeof(value));
		CHECK(value == 10);

		// OnUpdate should be a no-op (no method cached)
		comp->OnUpdate(0.016);
		valueField->Get(comp, &value, sizeof(value));
		CHECK(value == 10);

		// Hot reload: add OnUpdate method
		const char* scriptV2 = R"(
RegisterComponent("LuaHotReloadAdd", {
    value = 0,

    OnStart = function(self)
        self.value = 10
    end,

    OnUpdate = function(self, dt)
        self.value = self.value + 1
    end
})
)";
		CHECK(ScriptManager::LoadScript(scriptV2, ".lua"));

		// Now OnUpdate should work
		comp->OnUpdate(0.016);
		valueField->Get(comp, &value, sizeof(value));
		CHECK(value == 11);

		App::SetReloadEnabled(false);
	}

	// -----------------------------------------------------------------------
	// Hot Reload: New field added to component is visible via reflection
	// -----------------------------------------------------------------------

	TEST_CASE("LuaScript::HotReload::NewFieldAddedToReflection")
	{
		InitLuaScriptTestTypes();
		App::SetReloadEnabled(true);

		// V1: component with one field
		const char* scriptV1 = R"(
RegisterComponent("LuaHotReloadField", {
    health = 100
})
)";
		CHECK(ScriptManager::LoadScript(scriptV1, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaHotReloadField");
		REQUIRE(reflType != nullptr);

		// V1 should have exactly the "health" field
		CHECK(reflType->FindField("health") != nullptr);
		CHECK(reflType->FindField("armor") == nullptr);

		// V2: add a new field
		const char* scriptV2 = R"(
RegisterComponent("LuaHotReloadField", {
    health = 100,
    armor = 50
})
)";
		CHECK(ScriptManager::LoadScript(scriptV2, ".lua"));

		// Re-fetch the type — RegisterLuaClass creates a new reflection type version
		reflType = Reflection::FindTypeByName("LuaHotReloadField");
		REQUIRE(reflType != nullptr);

		// After hot reload, the new "armor" field should be visible
		ReflectField* armorField = reflType->FindField("armor");
		REQUIRE(armorField != nullptr);

		// Original field should still exist
		CHECK(reflType->FindField("health") != nullptr);

		// Create a new instance — it should have the new field with default value
		Scene scene;
		Entity* entity = scene.CreateEntity();
		REQUIRE(entity != nullptr);

		Component* comp = entity->AddComponent(reflType);
		REQUIRE(comp != nullptr);

		i32 armor = 0;
		armorField->Get(comp, &armor, sizeof(armor));
		CHECK(armor == 50);

		App::SetReloadEnabled(false);
	}

	TEST_CASE("LuaScript::HotReload::NewFieldAccessibleOnLiveInstance")
	{
		InitLuaScriptTestTypes();
		App::SetReloadEnabled(true);

		// V1: component with one field and OnUpdate that uses it
		const char* scriptV1 = R"(
RegisterComponent("LuaHotReloadLiveField", {
    value = 10,

    OnUpdate = function(self, dt)
        self.value = self.value + 1
    end
})
)";
		CHECK(ScriptManager::LoadScript(scriptV1, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaHotReloadLiveField");
		REQUIRE(reflType != nullptr);

		Scene scene;
		Entity* entity = scene.CreateEntity();
		REQUIRE(entity != nullptr);

		Component* comp = entity->AddComponent(reflType);
		REQUIRE(comp != nullptr);

		comp->OnUpdate(0.016);

		ReflectField* valueField = reflType->FindField("value");
		REQUIRE(valueField != nullptr);
		i32 value = 0;
		valueField->Get(comp, &value, sizeof(value));
		CHECK(value == 11);

		// V2: add a new field "bonus", and OnUpdate now sets it
		const char* scriptV2 = R"(
RegisterComponent("LuaHotReloadLiveField", {
    value = 10,
    bonus = 0,

    OnUpdate = function(self, dt)
        self.value = self.value + 1
        self.bonus = 99
    end
})
)";
		CHECK(ScriptManager::LoadScript(scriptV2, ".lua"));

		// Re-fetch the type — new fields create a new reflection type version
		reflType = Reflection::FindTypeByName("LuaHotReloadLiveField");
		REQUIRE(reflType != nullptr);

		// "bonus" field should now exist in reflection
		ReflectField* bonusField = reflType->FindField("bonus");
		REQUIRE(bonusField != nullptr);

		// Re-fetch valueField from the new type version
		valueField = reflType->FindField("value");
		REQUIRE(valueField != nullptr);

		// Run OnUpdate — it should set bonus on the live instance
		comp->OnUpdate(0.016);

		i32 bonus = 0;
		bonusField->Get(comp, &bonus, sizeof(bonus));
		CHECK(bonus == 99);

		// value should have incremented again
		valueField->Get(comp, &value, sizeof(value));
		CHECK(value == 12);

		App::SetReloadEnabled(false);
	}

	TEST_CASE("LuaScript::HotReload::ExistingFieldsPreserved")
	{
		InitLuaScriptTestTypes();
		App::SetReloadEnabled(true);

		const char* scriptV1 = R"(
RegisterComponent("LuaHotReloadPreserve", {
    score = 0,

    OnUpdate = function(self, dt)
        self.score = self.score + 10
    end
})
)";
		CHECK(ScriptManager::LoadScript(scriptV1, ".lua"));

		ReflectType* reflType = Reflection::FindTypeByName("LuaHotReloadPreserve");
		REQUIRE(reflType != nullptr);

		Scene scene;
		Entity* entity = scene.CreateEntity();
		Component* comp = entity->AddComponent(reflType);
		REQUIRE(comp != nullptr);

		// Accumulate some state
		comp->OnUpdate(0.016);
		comp->OnUpdate(0.016);

		ReflectField* scoreField = reflType->FindField("score");
		REQUIRE(scoreField != nullptr);
		i32 score = 0;
		scoreField->Get(comp, &score, sizeof(score));
		CHECK(score == 20);

		// Hot reload: add a new field, keep score
		const char* scriptV2 = R"(
RegisterComponent("LuaHotReloadPreserve", {
    score = 0,
    level = 1,

    OnUpdate = function(self, dt)
        self.score = self.score + 10
    end
})
)";
		CHECK(ScriptManager::LoadScript(scriptV2, ".lua"));

		// Re-fetch the type — new fields create a new reflection type version
		reflType = Reflection::FindTypeByName("LuaHotReloadPreserve");
		REQUIRE(reflType != nullptr);

		// Re-fetch scoreField from the new type version
		scoreField = reflType->FindField("score");
		REQUIRE(scoreField != nullptr);

		// Existing field value should be preserved on the live instance
		scoreField->Get(comp, &score, sizeof(score));
		CHECK(score == 20);

		// Continue updating — should still work
		comp->OnUpdate(0.016);
		scoreField->Get(comp, &score, sizeof(score));
		CHECK(score == 30);

		// New field should be accessible
		ReflectField* levelField = reflType->FindField("level");
		REQUIRE(levelField != nullptr);

		App::SetReloadEnabled(false);
	}
}
