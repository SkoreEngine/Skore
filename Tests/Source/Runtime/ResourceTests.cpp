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

			RID subobject = Resources::Create<ResourceTest>(UUID::RandomUUID());

			Array<RID> subobjects;
			for (int i = 0; i < 5; ++i)
			{
				subobjects.EmplaceBack(Resources::Create<ResourceTest>(UUID::RandomUUID()));
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
			}

			RID clone = Resources::Clone(test);
			{
				CHECK(clone != test);
				ResourceObject readClone = Resources::Read(clone);
				CHECK(readClone.GetInt(ResourceTest::IntValue) == 10);
				CHECK(readClone.GetString(ResourceTest::StringValue) == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
			}

			Resources::Destroy(clone);

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

	TEST_CASE("Resource::Prototypes")
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
		}

		RID prototype = Resources::Create<ResourceTest>();
		RID subobject1 = Resources::Create<ResourceTest>();
		RID subobject2 = Resources::Create<ResourceTest>();
		RID subobject3 = Resources::Create<ResourceTest>();

		{
			ResourceObject write = Resources::Write(prototype);
			write.SetInt(ResourceTest::IntValue, 10);
			write.SetString(ResourceTest::StringValue, "blegh");
			write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobject1);
			write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobject2);
			write.Commit();
		}

		RID item = Resources::CreateFromPrototype(prototype);

		{
			ResourceObject write = Resources::Write(item);
			write.SetInt(ResourceTest::IntValue, 222);
			write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobject3);
			write.RemoveFromPrototypeSubObjectSet(ResourceTest::SubObjectSet, subobject2);
			write.Commit();
		}

		{
			ResourceObject read = Resources::Read(item);
			CHECK(read.GetInt(ResourceTest::IntValue) == 222);
			CHECK(read.GetString(ResourceTest::StringValue) == "blegh");

			HashSet<RID> subobjects = ToHashSet<RID>(read.GetSubObjectSetAsArray(ResourceTest::SubObjectSet));
			CHECK(subobjects.Size() == 2);
			subobjects.Erase(subobject1);
			subobjects.Erase(subobject3);
			CHECK(subobjects.Size() == 0);
		}

		ResourceShutdown();
	}

	TEST_CASE("Resource::UndoRedo")
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
		}

		RID rid = Resources::Create<ResourceTest>();
		RID subobject = Resources::Create<ResourceTest>();
		RID subobject2 = Resources::Create<ResourceTest>();

		{
			ResourceObject write = Resources::Write(rid);
			write.SetInt(ResourceTest::IntValue, 10);
			write.SetString(ResourceTest::StringValue, "blegh");
			write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobject);
			write.Commit();
		}

		UndoRedoScope* scope = Resources::CreateScope("test scope");

		{
			ResourceObject write = Resources::Write(rid);
			write.SetInt(ResourceTest::IntValue, 33);
			write.SetString(ResourceTest::StringValue, "44");
			write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobject2);
			write.Commit(scope);
		}

		{
			ResourceObject read = Resources::Read(rid);
			CHECK(read.GetInt(ResourceTest::IntValue) == 33);
			CHECK(read.GetString(ResourceTest::StringValue) == "44");

			HashSet<RID> subobjects = ToHashSet<RID>(read.GetSubObjectSetAsArray(ResourceTest::SubObjectSet));
			CHECK(subobjects.Size() == 2);
			subobjects.Erase(subobject);
			subobjects.Erase(subobject2);
			CHECK(subobjects.Size() == 0);

		}

		Resources::Undo(scope);

		{
			ResourceObject read = Resources::Read(rid);
			CHECK(read.GetInt(ResourceTest::IntValue) == 10);
			CHECK(read.GetString(ResourceTest::StringValue) == "blegh");

			HashSet<RID> subobjects = ToHashSet<RID>(read.GetSubObjectSetAsArray(ResourceTest::SubObjectSet));
			CHECK(subobjects.Size() == 1);
			subobjects.Erase(subobject);
			CHECK(subobjects.Size() == 0);
		}

		ResourceShutdown();
	}

	TEST_CASE("Resource::Serialization")
	{

		UUID uuids[6] = {
			UUID::RandomUUID(),
			UUID::RandomUUID(),
			UUID::RandomUUID(),
			UUID::RandomUUID(),
			UUID::RandomUUID(),
			UUID::RandomUUID()
		};

		String yaml = "";
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
			}

			{

				RID rid = Resources::Create<ResourceTest>(uuids[0]);

				ResourceObject write = Resources::Write(rid);
				write.SetInt(ResourceTest::IntValue, 33);
				write.SetString(ResourceTest::StringValue, "44");


				for (u32 i = 0; i < 5; ++i)
				{
					RID subobject = Resources::Create<ResourceTest>(uuids[i+1]);
					ResourceObject subObjectWrite = Resources::Write(subobject);
					subObjectWrite.SetInt(ResourceTest::IntValue, i);
					subObjectWrite.Commit();

					write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobject);
				}

				write.Commit();

				YamlArchiveWriter writer;
				Resources::Serialize(rid, writer);
				yaml = writer.EmitAsString();
			}
			ResourceShutdown();
		}

		REQUIRE(yaml.Size() > 0);
		//std::cout << yaml.CStr() << std::endl;

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
			}

			YamlArchiveReader reader(yaml.CStr());
			RID newResource = Resources::Deserialize(reader);
			CHECK(newResource);

			ResourceObject read = Resources::Read(newResource);
			CHECK(read.GetUUID() == uuids[0]);
			CHECK(read.GetInt(ResourceTest::IntValue) == 33);
			CHECK(read.GetString(ResourceTest::StringValue) == "44");

			Array<RID> subobjects = read.GetSubObjectSetAsArray(ResourceTest::SubObjectSet);
			CHECK(subobjects.Size() == 5);

			for (u32 i = 0; i < subobjects.Size(); ++i)
			{
				ResourceObject subRead = Resources::Read(subobjects[i]);
				CHECK(subRead.GetUUID() == uuids[i + 1]);
				CHECK(subRead.GetInt(ResourceTest::IntValue) == i);
			}

			ResourceShutdown();
		}
	}
}
