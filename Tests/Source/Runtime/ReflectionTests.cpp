// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include "doctest.h"
#include "Skore/App.hpp"
#include "Skore/Core/Reflection.hpp"

using namespace Skore;

namespace
{
	struct AttributeTest
	{
		u32 value;
	};

	enum class TestEnum
	{
		None = 0,
		Value1 = 1,
		Value2 = 2
	};

	struct TestBaseType : Object
	{
		SK_CLASS(TestBaseType, Object)

		TestBaseType() = default;
		TestBaseType(i32 value) : value(value) {}

		i32 value = 0;

		static void RegisterType(NativeReflectType<TestBaseType>& type)
		{
			type.Field<&TestBaseType::value>("value");
		}
	};


	struct TestReflectionType : TestBaseType
	{
		SK_CLASS(TestReflectionType, TestBaseType)

		u32 anotherValue = 10;
		u32 test;

		TestReflectionType() = default;

		TestReflectionType(i32 value) : TestBaseType(value) {}

		i32 MyFunc(i32 otherValue)
		{
			value += otherValue;
			return value * 2;
		}

		i32 MyFunc(i32 value1, i32 value2)
		{
			value += value1 * value2;
			return value * 2;
		}

		const i32& GetValue() const
		{
			return value;
		}

		void Dup()
		{
			value = value * 2;
		}

		const u32& GetTest() const
		{
			return test;
		}

		void SetTest(u32 pTest)
		{
			test = pTest;
		}

		static void RegisterType(NativeReflectType<TestReflectionType>& type)
		{
			type.Attribute<AttributeTest>(AttributeTest{42});
			type.Constructor<i32>("value");
			type.Field<&TestReflectionType::anotherValue>("anotherValue");
			type.Field<&TestReflectionType::test, &TestReflectionType::GetTest, &TestReflectionType::SetTest>("test");
			type.Function<static_cast<i32(TestReflectionType::*)(i32)>(&TestReflectionType::MyFunc)>("MyFunc", "otherValue");
			type.Function<static_cast<i32(TestReflectionType::*)(i32, i32)>(&TestReflectionType::MyFunc)>("MyFunc", "value1", "value2");
			type.Function<&TestReflectionType::Dup>("Dup");
			type.Function<&TestReflectionType::GetValue>("GetValue");
		}
	};

	struct AnotherObject : Object
	{
		SK_CLASS(AnotherObject, Object)
	};


	TEST_CASE("Core::Reflection")
	{
		App::ResetContext();

		Reflection::Type<AnotherObject>();
		Reflection::Type<TestBaseType>();
		Reflection::Type<TestReflectionType>();
		Reflection::Type<AttributeTest>();

		{
			auto testEnum = Reflection::Type<TestEnum>();
			testEnum.Value<TestEnum::None>();
			testEnum.Value<TestEnum::Value1>();
			testEnum.Value<TestEnum::Value2>();
		}

		{
			Array<TypeID> types = Reflection::GetDerivedTypes(TypeInfo<TestBaseType>::ID());
			REQUIRE(types.Size() == 1);
			CHECK(types[0] == TypeInfo<TestReflectionType>::ID());
		}

		ReflectType* type = Reflection::FindType<TestReflectionType>();
		REQUIRE(type);
		CHECK(!type->GetName().Empty());

		{
			ReflectField* valueField = type->FindField("value");
			REQUIRE(valueField);
		}

		{
			Span<ReflectFunction*> funcs = type->FindFunctionByName("MyFunc");
			CHECK(funcs.Size() == 2);
		}

		{
			ReflectFunction* func = type->FindFunction("MyFunc", {TypeInfo<i32>::ID()});
			CHECK(func);
		}

		{
			ReflectFunction* func = type->FindFunction("MyFunc", {TypeInfo<i32>::ID(), TypeInfo<i32>::ID()});
			CHECK(func);
		}

		{
			Span<ReflectConstructor*> c = type->GetConstructors();
			CHECK(c.Size() == 2);
		}

		{
			ReflectConstructor* c = type->FindConstructor({TypeInfo<i32>::ID()});
			REQUIRE(c);
			CHECK(c->GetParams().Size() == 1);
			CHECK(c->GetParams()[0]->GetProps().typeId == TypeInfo<i32>::ID());

			TestReflectionType valueType = {};
			i32                value = 33;
			VoidPtr            params[1] = {&value};
			c->Construct(&valueType, params);
			CHECK(valueType.value == 33);
		}

		{
			ReflectConstructor* c = type->FindConstructor({});
			REQUIRE(c);
			CHECK(c->GetParams().Size() == 0);
		}

		{
			ReflectConstructor* c = type->FindConstructor({TypeInfo<String>::ID()});
			CHECK(c == nullptr);
		}

		{
			Object* object = type->NewObject(MemoryGlobals::GetDefaultAllocator(), 45);
			CHECK(object->GetTypeId() == TypeInfo<TestReflectionType>::ID());
			CHECK(object->GetType() == type);

			AnotherObject* anotherObject = object->SafeCast<AnotherObject>();
			CHECK(!anotherObject);

			TestBaseType* testBase = object->SafeCast<TestBaseType>();
			CHECK(testBase);

			TestReflectionType* valueType = object->Cast<TestReflectionType>();

			REQUIRE(valueType);
			CHECK(valueType->value == 45);
			type->Destroy(valueType);
		}

		{
			TestReflectionType a;
			ReflectField* field = type->FindField("test");
			REQUIRE(field);
			field->Set(&a, 10);
			CHECK(a.test == 10);

			u32 getValue = 0;
			field->Get(&a, getValue);
			CHECK(getValue == 10);
		}

		{
			TestReflectionType a(10);
			ReflectFunction*   func = type->FindFunction("MyFunc", {TypeInfo<i32>::ID(), TypeInfo<i32>::ID()});

			i32     value1 = 3;
			i32     value2 = 3;
			VoidPtr params[2] = {&value1, &value2};

			i32 ret = 0;
			func->Invoke(&a, &ret, params);
			CHECK(ret == 38);
			CHECK(a.value == 19);
		}
		{
			ReflectFunction* func = type->FindFunction("GetValue");

			TestReflectionType a(10);
			i32 ret = 0;
			func->Invoke(&a, &ret, nullptr);
			CHECK(ret == 10);

		}
		{
			const AttributeTest* attrValue = type->GetAttribute<AttributeTest>();
			REQUIRE(attrValue);
			CHECK(attrValue->value == 42);
		}

		{
			ReflectType* enumTest = Reflection::FindType<TestEnum>();

			CHECK(enumTest->GetValues().Size() == 3);

			ReflectValue* value = enumTest->FindValueByName("Value1");
			REQUIRE(value);
			CHECK(value->GetCode() == 1);
			CHECK(value->GetDesc() == "Value1");

		}
	}
}