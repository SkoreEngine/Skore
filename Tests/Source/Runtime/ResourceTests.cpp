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

#include <iostream>
#include <ostream>

#include "doctest.h"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Serialization.hpp"
#include "Skore/Resource/Resources.hpp"

using namespace Skore;


namespace Skore
{
	void SK_API ResourceInit();
	void SK_API ResourceShutdown();
}

namespace
{
	struct ResourceTest
	{
		enum
		{
			BoolValue,
			StringValue,
			IntValue,
			SubObject,
			SubObjectSet
		};
	};

	TEST_CASE("Resource::Basics")
	{
		ResourceInit();
		{
			Resources::Type<ResourceTest>()
				.Field<ResourceTest::BoolValue>(ResourceFieldType::Bool)
				.Field<ResourceTest::StringValue>(ResourceFieldType::String)
				.Field<ResourceTest::IntValue>(ResourceFieldType::Int)
				.Field<ResourceTest::SubObject>(ResourceFieldType::SubObject)
				.Field<ResourceTest::SubObjectSet>(ResourceFieldType::SubObjectSet)
				.Build();

			RID test = Resources::Create<ResourceTest>(UUID::RandomUUID());
			CHECK(test.id == 1);

			RID subobject = Resources::Create<ResourceTest>();

			Array<RID> subobjects;
			for (int i = 0; i < 5; ++i)
			{
				subobjects.EmplaceBack(Resources::Create<ResourceTest>());
			}

			{
				ResourceObject write = Resources::Write(subobject);
				write.SetString(ResourceTest::StringValue, "stringsubojbect");
				write.Commit();
			}

			{
				for (int i = 0; i < 5; ++i)
				{
					ResourceObject write = Resources::Write(subobjects[i]);
					write.SetInt(ResourceTest::IntValue, i);
					write.SetString(ResourceTest::StringValue, "str");
					write.Commit();
				}
			}

			{
				ResourceObject write = Resources::Write(test);
				CHECK(!write.HasValue(ResourceTest::BoolValue));
				CHECK(!write.HasValue(ResourceTest::StringValue));
				CHECK(!write.HasValue(ResourceTest::IntValue));

				write.SetInt(ResourceTest::IntValue, 10);
				write.SetString(ResourceTest::StringValue, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
				write.SetSubObject(ResourceTest::SubObject, subobject);

				for (int i = 0; i < 5; ++i)
				{
					write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobjects[i]);
				}

				write.Commit();
			}

			Resources::GarbageCollect();

			{
				ResourceObject read = Resources::Read(test);

				CHECK(!read.HasValue(ResourceTest::BoolValue));
				CHECK(read.HasValue(ResourceTest::StringValue));
				CHECK(read.HasValue(ResourceTest::IntValue));

				CHECK(read.GetInt(ResourceTest::IntValue) == 10);
				CHECK(read.GetString(ResourceTest::StringValue) == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

				YamlArchiveWriter writer;
				Resources::Serialize(test, writer);
				String str = writer.EmitAsString();

				//TODO complete tests
				CHECK(str.Size() > 0);
				// std::cout << str.CStr() << std::endl;

				YamlArchiveReader reader(str.CStr());
				RID newResource = Resources::Deserialize(reader);

			//	CHECK(newResource);

			}
		}
		ResourceShutdown();
	}

	struct CompositionStruct
	{
		i32 value = 1;
		f32 anotherValue = 1;

		static void RegisterType(NativeReflectType<CompositionStruct>& type)
		{
			type.Field<&CompositionStruct::value>("value");
			type.Field<&CompositionStruct::anotherValue>("anotherValue");
		}
	};

	struct StructToCast
	{
		i32               intValue = 1;
		String            strValue = "";
		CompositionStruct composition;

		static void RegisterType(NativeReflectType<StructToCast>& type)
		{
			type.Field<&StructToCast::intValue>("intValue");
			type.Field<&StructToCast::strValue>("strValue");
			type.Field<&StructToCast::composition>("composition");
		}
	};

	TEST_CASE("Resource::Casters")
	{
		ResourceInit();

		{
			Reflection::Type<StructToCast>();
			Reflection::Type<CompositionStruct>();
		}

		{
			RID rid = Resources::Create<StructToCast>();
			CHECK(rid);

			StructToCast value;
			value.intValue = 10;
			value.strValue = "test";
			value.composition.value = 303;
			value.composition.anotherValue = 305.0f;

			Resources::ToResource(rid, &value);

			ResourceObject obj = Resources::Read(rid);
			CHECK(obj.GetInt(0) == 10);
			CHECK(obj.GetString(1) == "test");

			StructToCast anotherValue;
			Resources::FromResource(rid, &anotherValue);

			CHECK(anotherValue.intValue == value.intValue);
			CHECK(anotherValue.strValue == value.strValue);
			CHECK(anotherValue.composition.value == value.composition.value);
			CHECK(anotherValue.composition.anotherValue == value.composition.anotherValue);
		}

		ResourceShutdown();
	}
}
