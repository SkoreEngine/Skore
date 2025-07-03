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
			SubObjectSet,
			RefArray
		};
	};

	struct WrongIndex
	{
		enum
		{
			SubObject,
			Value1,
			Value2
		};
	};

	void RegisterTestTypes()
	{
		Resources::Type<ResourceTest>()
			.Field<ResourceTest::BoolValue>(ResourceFieldType::Bool)
			.Field<ResourceTest::StringValue>(ResourceFieldType::String)
			.Field<ResourceTest::IntValue>(ResourceFieldType::Int)
			.Field<ResourceTest::SubObject>(ResourceFieldType::SubObject)
			.Field<ResourceTest::SubObjectSet>(ResourceFieldType::SubObjectSet)
			.Field<ResourceTest::RefArray>(ResourceFieldType::ReferenceArray)
			.Build();
	}


	TEST_CASE("Resource::DefaultValues")
	{
		ResourceInit();
		{
			RegisterTestTypes();

			RID            defaultValue = Resources::Create<ResourceTest>();
			ResourceObject write = Resources::Write(defaultValue);
			write.SetString(ResourceTest::StringValue, "strtest");
			write.SetInt(ResourceTest::IntValue, 42);
			write.SetBool(ResourceTest::BoolValue, true);
			write.Commit();

			ResourceType* resourceType = Resources::FindType<ResourceTest>();
			resourceType->SetDefaultValue(defaultValue);
		}

		RID rid = Resources::Create<ResourceTest>();

		ResourceObject read = Resources::Read(rid);
		CHECK(read.GetInt(ResourceTest::IntValue) == 42);
		CHECK(read.GetString(ResourceTest::StringValue) == "strtest");
		CHECK(read.GetBool(ResourceTest::BoolValue) == true);

		ResourceShutdown();
	}


	TEST_CASE("Resource::AllBasics")
	{
		ResourceInit();
		{
			RegisterTestTypes();

			RID test = Resources::Create<ResourceTest>(UUID::RandomUUID());
			CHECK(test);

			RID subobject = Resources::Create<ResourceTest>(UUID::RandomUUID());

			Array<RID> subobjects;
			for (int i = 0; i < 5; ++i)
			{
				subobjects.EmplaceBack(Resources::Create<ResourceTest>(UUID::RandomUUID()));
			}
			Array<RID> refs;
			for (int i = 0; i < 5; ++i)
			{
				refs.EmplaceBack(Resources::Create<ResourceTest>(UUID::RandomUUID()));
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
				write.SetReferenceArray(ResourceTest::RefArray, refs);
				write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobjects);

				write.Commit();
			}

			Resources::GarbageCollect();

			ResourceObject read = Resources::Read(test);
			CHECK(!read.HasValue(ResourceTest::BoolValue));
			CHECK(read.HasValue(ResourceTest::StringValue));
			CHECK(read.HasValue(ResourceTest::IntValue));

			CHECK(read.GetInt(ResourceTest::IntValue) == 10);
			CHECK(read.GetString(ResourceTest::StringValue) == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
			CHECK(read.GetReferenceArray(ResourceTest::RefArray) == refs);
			CHECK(read.GetSubObject(ResourceTest::SubObject) == subobject);
		}
		ResourceShutdown();
	}

	TEST_CASE("Resource::Clone")
	{
		ResourceInit();
		{
			RegisterTestTypes();

			RID subobject = Resources::Create<ResourceTest>();
			{
				ResourceObject write = Resources::Write(subobject);
				write.SetString(ResourceTest::StringValue, "subobject");
				write.Commit();
			}

			RID subobjectToSet = Resources::Create<ResourceTest>();
			{
				ResourceObject write = Resources::Write(subobjectToSet);
				write.SetString(ResourceTest::StringValue, "subobjectToSet");
				write.Commit();
			}

			RID rid = Resources::Create<ResourceTest>();
			CHECK(rid.id);

			ResourceObject write = Resources::Write(rid);
			write.SetInt(ResourceTest::IntValue, 10);
			write.SetString(ResourceTest::StringValue, "blegh");
			write.SetSubObject(ResourceTest::SubObject, subobject);
			write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobjectToSet);
			write.Commit();

			RID clone = Resources::Clone(rid);
			CHECK(clone != rid);

			ResourceObject readClone = Resources::Read(clone);
			CHECK(readClone.GetInt(ResourceTest::IntValue) == 10);
			CHECK(readClone.GetString(ResourceTest::StringValue) == "blegh");

			{
				RID subobjectClone = readClone.GetSubObject(ResourceTest::SubObject);
				CHECK(subobjectClone != subobject);

				ResourceObject subobjectReadClone = Resources::Read(subobjectClone);
				CHECK(subobjectReadClone.GetString(ResourceTest::StringValue) == "subobject");
			}


			HashSet<RID> arr = ToHashSet<RID>(readClone.GetSubObjectSetAsArray(ResourceTest::SubObjectSet));
			REQUIRE(arr.Size() == 1);
			arr.Erase(subobjectToSet);
			REQUIRE(arr.Size() == 1);

			auto it = arr.begin();

			{
				RID subobjectClone = *it;
				CHECK(subobjectClone != subobjectToSet);

				ResourceObject subobjectReadClone = Resources::Read(subobjectClone);
				CHECK(subobjectReadClone.GetString(ResourceTest::StringValue) == "subobjectToSet");
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
		i32               intValue = 42;
		String            strValue = "default";
		CompositionStruct composition;

		static void RegisterType(NativeReflectType<StructToCast>& type)
		{
			type.Field<&StructToCast::intValue>("intValue");
			type.Field<&StructToCast::strValue>("strValue");
			type.Field<&StructToCast::composition>("composition");
		}
	};

	TEST_CASE("Resource::WrongIndex")
	{
		ResourceInit();
		{
			Resources::Type<WrongIndex>()
				.Field<WrongIndex::SubObject>(ResourceFieldType::SubObject)
				.Field<WrongIndex::Value2>(ResourceFieldType::ReferenceArray)
				.Field<WrongIndex::Value1>(ResourceFieldType::ReferenceArray)
				.Build();

			RID object = Resources::Create<WrongIndex>();
			RID sub = Resources::Create<WrongIndex>();
			RID ref1 = Resources::Create<WrongIndex>();
			RID ref2 = Resources::Create<WrongIndex>();

			{
				ResourceObject obj = Resources::Write(object);
				obj.SetSubObject(WrongIndex::SubObject, sub);
				obj.Commit();
			}

			{
				ResourceObject obj = Resources::Write(object);
				obj.AddToReferenceArray(WrongIndex::Value1, ref1);
				obj.Commit();
			}

			{
				ResourceObject obj = Resources::Write(object);
				obj.AddToReferenceArray(WrongIndex::Value1, ref2);
				obj.Commit();
			}

			{
				ResourceObject obj = Resources::Read(object);
				Span<RID> rids = obj.GetReferenceArray(WrongIndex::Value1);
				CHECK(rids.Size() == 2);
			}
		}
		ResourceShutdown();
	}

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

			{
				ResourceObject obj = Resources::Read(rid);
				CHECK(obj.GetInt(0) == 42);
				CHECK(obj.GetString(1) == "default");
			}

			StructToCast value = {};
			StructToCast anotherValue = {};
			Resources::FromResource(rid, &anotherValue);

			CHECK(anotherValue.intValue == value.intValue);
			CHECK(anotherValue.strValue == value.strValue);
			CHECK(anotherValue.composition.value == value.composition.value);
			CHECK(anotherValue.composition.anotherValue == value.composition.anotherValue);
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
		RegisterTestTypes();


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

		RegisterTestTypes();

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

	TEST_CASE("Resource::Subobjects")
	{
		ResourceInit();
		RegisterTestTypes();

		{
			auto populate = [](RID rid)
			{
				ResourceObject write = Resources::Write(rid);
				write.SetString(ResourceTest::StringValue, "StrintString");
				write.Commit();
			};

			RID object = Resources::Create<ResourceTest>();
			RID subObject1 = Resources::Create<ResourceTest>();
			RID subObject2 = Resources::Create<ResourceTest>();
			RID subObject3 = Resources::Create<ResourceTest>();

			populate(object);
			populate(subObject1);
			populate(subObject2);
			populate(subObject3);

			ResourceObject write = Resources::Write(object);
			write.SetSubObject(ResourceTest::SubObject, subObject1);
			write.AddToSubObjectSet(ResourceTest::SubObjectSet, subObject2);
			write.AddToSubObjectSet(ResourceTest::SubObjectSet, subObject3);
			write.Commit();

			Resources::Destroy(subObject3);

			ResourceObject read = Resources::Write(object);
			CHECK(!read.HasSubObjectSet(ResourceTest::SubObjectSet, subObject3));

			CHECK(Resources::HasValue(object));
			CHECK(Resources::HasValue(subObject1));
			CHECK(Resources::HasValue(subObject2));

			Resources::Destroy(object);

			CHECK(!Resources::HasValue(object));
			CHECK(!Resources::HasValue(subObject1));
			CHECK(!Resources::HasValue(subObject2));

			Resources::GarbageCollect();
		}
		ResourceShutdown();
	}

	TEST_CASE("Resource::TestInstances")
	{
		ResourceInit();
		RegisterTestTypes();
		{
			RID object = Resources::Create<ResourceTest>();
			RID subobject1 = Resources::Create<ResourceTest>();
			RID subobject2 = Resources::Create<ResourceTest>();

			{
				ResourceObject write = Resources::Write(subobject1);
				write.SetInt(ResourceTest::IntValue, 10);
				write.Commit();
			}

			{
				ResourceObject write = Resources::Write(subobject1);
				write.SetInt(ResourceTest::IntValue, 20);
				write.Commit();
			}

			{
				ResourceObject write = Resources::Write(object);
				write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobject1);
				write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobject2);
				write.Commit();
			}

			RID prototype = Resources::CreateFromPrototype(object);
			RID instance1;

			{
				ResourceObject write = Resources::Write(prototype);
				instance1 = write.InstantiateFromSubObjectSet(ResourceTest::SubObjectSet, subobject1);
				write.Commit();
			}

			CHECK(instance1);

			{
				ResourceObject readPrototype = Resources::Read(prototype);
				REQUIRE(readPrototype);

				HashSet<RID> set = readPrototype.GetSubObjectSetAsHashSet(ResourceTest::SubObjectSet);
				CHECK(set.Has(instance1));
				CHECK(!set.Has(subobject1));
				CHECK(set.Has(subobject2));
			}

			//test clone.
			{
				RID prototypeClone = Resources::Clone(prototype);
				ResourceObject readPrototype = Resources::Read(prototypeClone);
				REQUIRE(readPrototype);

				HashSet<RID> set = readPrototype.GetSubObjectSetAsHashSet(ResourceTest::SubObjectSet);

				CHECK(!set.Has(instance1));
				CHECK(!set.Has(subobject1));
				CHECK(set.Has(subobject2));

				set.Erase(subobject2);

				for (RID rid : set)
				{
					//instance1 needs to be cloned from the same prototype.
					CHECK(Resources::GetStorage(rid)->prototype == Resources::GetStorage(instance1)->prototype);
				}
			}

			{
				ResourceObject write = Resources::Write(prototype);
				write.RemoveInstanceFromSubObjectSet(ResourceTest::SubObjectSet, instance1);
				write.Commit();
			}

			{
				ResourceObject readPrototype = Resources::Read(prototype);
				HashSet<RID> set = readPrototype.GetSubObjectSetAsHashSet(ResourceTest::SubObjectSet);
				CHECK(!set.Has(instance1));
				CHECK(set.Has(subobject1));
				CHECK(set.Has(subobject2));
			}
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

		HashMap<u32, UUID> uuidToIndex;

		String yaml = "";
		{
			ResourceInit();

			RegisterTestTypes();

			{
				RID rid = Resources::Create<ResourceTest>(uuids[0]);

				ResourceObject write = Resources::Write(rid);
				write.SetInt(ResourceTest::IntValue, 33);
				write.SetString(ResourceTest::StringValue, "44");


				for (u32 i = 0; i < 5; ++i)
				{
					RID subobject = Resources::Create<ResourceTest>(uuids[i + 1]);
					ResourceObject subObjectWrite = Resources::Write(subobject);
					subObjectWrite.SetInt(ResourceTest::IntValue, i);
					subObjectWrite.Commit();

					write.AddToSubObjectSet(ResourceTest::SubObjectSet, subobject);

					uuidToIndex.Insert(i, uuids[i + 1]);
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
			RegisterTestTypes();

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
				if (auto it = uuidToIndex.Find(i))
				{
					RID rid = Resources::FindByUUID(it->second);
					ResourceObject subRead = Resources::Read(rid);
					CHECK(subRead.GetUUID() == uuids[i + 1]);
					CHECK(subRead.GetInt(ResourceTest::IntValue) == i);
				}
			}

			ResourceShutdown();
		}
	}
}
