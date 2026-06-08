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
			RefArray,
			Reference,
			SubObjectList
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

	struct ResourceTypeExportImport
	{
		enum
		{
			Name,
			Count,
			Ref
		};
	};

	void RegisterTestTypes()
	{
		Resources::Type<ResourceTest>()
			.Field<ResourceTest::BoolValue>(ResourceFieldType::Bool)
			.Field<ResourceTest::StringValue>(ResourceFieldType::String)
			.Field<ResourceTest::IntValue>(ResourceFieldType::Int)
			.Field<ResourceTest::SubObject>(ResourceFieldType::SubObject)
			.Field<ResourceTest::RefArray>(ResourceFieldType::ReferenceArray)
			.Field<ResourceTest::Reference>(ResourceFieldType::Reference)
			.Field<ResourceTest::SubObjectList>(ResourceFieldType::SubObjectList)
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
				write.AddToSubObjectList(ResourceTest::SubObjectList, subobjects);

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

			RID subobjectToList = Resources::Create<ResourceTest>();
			{
				ResourceObject write = Resources::Write(subobjectToList);
				write.SetString(ResourceTest::StringValue, "subobjectToSet");
				write.Commit();
			}

			RID rid = Resources::Create<ResourceTest>();
			CHECK(rid.id);

			ResourceObject write = Resources::Write(rid);
			write.SetInt(ResourceTest::IntValue, 10);
			write.SetString(ResourceTest::StringValue, "blegh");
			write.SetSubObject(ResourceTest::SubObject, subobject);
			write.AddToSubObjectList(ResourceTest::SubObjectList, subobjectToList);
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


			HashSet<RID> arr = ToHashSet<RID>(readClone.GetSubObjectList(ResourceTest::SubObjectList));
			REQUIRE(arr.Size() == 1);
			arr.Erase(subobjectToList);
			REQUIRE(arr.Size() == 1);

			auto it = arr.begin();

			{
				RID subobjectClone = *it;
				CHECK(subobjectClone != subobjectToList);

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
				Span<RID>      rids = obj.GetReferenceArray(WrongIndex::Value1);
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

	// TEST_CASE("Resource::Prototypes")
	// {
	// 	ResourceInit();
	// 	RegisterTestTypes();
	//
	//
	// 	RID prototype = Resources::Create<ResourceTest>();
	// 	RID subobject1 = Resources::Create<ResourceTest>();
	// 	RID subobject2 = Resources::Create<ResourceTest>();
	// 	RID subobject3 = Resources::Create<ResourceTest>();
	//
	// 	{
	// 		ResourceObject write = Resources::Write(prototype);
	// 		write.SetInt(ResourceTest::IntValue, 10);
	// 		write.SetString(ResourceTest::StringValue, "blegh");
	// 		write.AddToSubObjectList(ResourceTest::SubObjectList, subobject1);
	// 		write.AddToSubObjectList(ResourceTest::SubObjectList, subobject2);
	// 		write.Commit();
	// 	}
	//
	// 	RID item = Resources::CreateFromPrototype(prototype);
	//
	// 	{
	// 		ResourceObject write = Resources::Write(item);
	// 		write.SetInt(ResourceTest::IntValue, 222);
	// 		write.AddToSubObjectList(ResourceTest::SubObjectList, subobject3);
	// 		write.RemoveFromPrototypeSubObjectSet(ResourceTest::SubObjectList, subobject2);
	// 		write.Commit();
	// 	}
	//
	// 	{
	// 		ResourceObject read = Resources::Read(item);
	// 		CHECK(read.GetInt(ResourceTest::IntValue) == 222);
	// 		CHECK(read.GetString(ResourceTest::StringValue) == "blegh");
	//
	// 		HashSet<RID> subobjects = ToHashSet<RID>(read.GetSubObjectList(ResourceTest::SubObjectList));
	// 		CHECK(subobjects.Size() == 2);
	// 		subobjects.Erase(subobject1);
	// 		subobjects.Erase(subobject3);
	// 		CHECK(subobjects.Size() == 0);
	// 	}
	//
	// 	ResourceShutdown();
	// }

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
			write.AddToSubObjectList(ResourceTest::SubObjectList, subobject);
			write.Commit();
		}

		UndoRedoScope* scope = Resources::CreateScope("test scope");

		{
			ResourceObject write = Resources::Write(rid);
			write.SetInt(ResourceTest::IntValue, 33);
			write.SetString(ResourceTest::StringValue, "44");
			write.AddToSubObjectList(ResourceTest::SubObjectList, subobject2);
			write.Commit(scope);
		}

		{
			ResourceObject read = Resources::Read(rid);
			CHECK(read.GetInt(ResourceTest::IntValue) == 33);
			CHECK(read.GetString(ResourceTest::StringValue) == "44");

			HashSet<RID> subobjects = ToHashSet<RID>(read.GetSubObjectList(ResourceTest::SubObjectList));
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

			HashSet<RID> subobjects = ToHashSet<RID>(read.GetSubObjectList(ResourceTest::SubObjectList));
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
			write.AddToSubObjectList(ResourceTest::SubObjectList, subObject2);
			write.AddToSubObjectList(ResourceTest::SubObjectList, subObject3);
			write.Commit();

			Resources::Destroy(subObject3);

			ResourceObject read = Resources::Write(object);
			CHECK(!read.HasOnSubObjectList(ResourceTest::SubObjectList, subObject3));

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

	TEST_CASE("Resource::SubObjectListBasic")
	{
		ResourceInit();
		{
			RegisterTestTypes();

			RID object = Resources::Create<ResourceTest>();

			RID subObject = Resources::Create<ResourceTest>();
			RID subObject2 = Resources::Create<ResourceTest>();
			RID subObject3 = Resources::Create<ResourceTest>();

			{
				ResourceObject write = Resources::Write(object);
				write.AddToSubObjectList(ResourceTest::SubObjectList, subObject);
				write.AddToSubObjectList(ResourceTest::SubObjectList, subObject2);
				write.AddToSubObjectList(ResourceTest::SubObjectList, subObject3);
				write.Commit();
			}

			{
				ResourceObject read = Resources::Read(object);
				Array<RID>     list = read.GetSubObjectList(ResourceTest::SubObjectList);
				REQUIRE(list.Size() == 3);
				CHECK(list[0] == subObject);
				CHECK(list[1] == subObject2);
				CHECK(list[2] == subObject3);

				auto checkParent = [&](RID rid) -> bool
				{
					if (ResourceStorage* parent = Resources::GetStorage(rid)->parent)
					{
						return parent->rid == object;
					}
					return false;
				};

				CHECK(checkParent(subObject));
				CHECK(checkParent(subObject2));
				CHECK(checkParent(subObject3));
			}

			{
				ResourceObject write = Resources::Write(object);
				write.RemoveFromSubObjectList(ResourceTest::SubObjectList, subObject2);
				write.Commit();
			}

			{
				ResourceObject read = Resources::Read(object);
				Array<RID>     list = read.GetSubObjectList(ResourceTest::SubObjectList);
				CHECK(list.Size() == 2);
				CHECK(list[0] == subObject);
				CHECK(list[1] == subObject3);
			}

			{
				Resources::Destroy(subObject);
			}

			{
				ResourceObject read = Resources::Read(object);
				Array<RID>     list = read.GetSubObjectList(ResourceTest::SubObjectList);
				CHECK(list.Size() == 1);
				CHECK(list[0] == subObject3);
			}

			Resources::Destroy(object);

			{
				CHECK(!Resources::HasValue(object));
				CHECK(!Resources::HasValue(subObject3));
			}
		}

		ResourceShutdown();
	}

#if 0
	TEST_CASE("Resource::SubObjectListPrototypes")
	{
		ResourceInit();
		RegisterTestTypes();

		{
			RID prototype = Resources::Create<ResourceTest>();

			RID subobject1 = Resources::Create<ResourceTest>();
			RID subobject2 = Resources::Create<ResourceTest>();
			RID subobject3 = Resources::Create<ResourceTest>();


			{
				ResourceObject write = Resources::Write(subobject1);
				write.SetReference(ResourceTest::Reference, prototype);
				write.Commit();
			}


			{
				ResourceObject write = Resources::Write(prototype);
				write.SetInt(ResourceTest::IntValue, 10);
				write.SetString(ResourceTest::StringValue, "blegh");
				write.AddToSubObjectList(ResourceTest::SubObjectList, subobject1);
				write.AddToSubObjectList(ResourceTest::SubObjectList, subobject2);
				write.Commit();
			}

			RID item = Resources::CreateFromPrototype(prototype);

			{
				ResourceObject read = Resources::Read(item);
				Array<RID> arr = read.GetSubObjectList(ResourceTest::SubObjectList);
				REQUIRE(arr.Size() == 2);
				CHECK(arr[0] != subobject1);
				CHECK(arr[1] != subobject2);

				CHECK(Resources::GetStorage(arr[0])->prototype == Resources::GetStorage(subobject1));
				CHECK(Resources::GetStorage(arr[1])->prototype == Resources::GetStorage(subobject2));


				{
					ResourceObject subobjectWrite = Resources::Write(arr[0]);
					subobjectWrite.SetString(ResourceTest::StringValue, "str");
					subobjectWrite.Commit();
				}

				ResourceObject subobjectRead = Resources::Read(arr[0]);
				CHECK(subobjectRead.GetReference(ResourceTest::Reference) == prototype);
			}

			{
				ResourceObject write = Resources::Write(item);

				Array<RID> arr = write.GetSubObjectList(ResourceTest::SubObjectList);

				write.SetInt(ResourceTest::IntValue, 222);
				write.AddToSubObjectList(ResourceTest::SubObjectList, subobject3);
				write.RemoveFromSubObjectList(ResourceTest::SubObjectList, arr[1]);
				write.Commit();
			}

			{
				ResourceObject read = Resources::Read(item);
				CHECK(read.GetInt(ResourceTest::IntValue) == 222);
				CHECK(read.GetString(ResourceTest::StringValue) == "blegh");

				Array<RID> items = read.GetSubObjectList(ResourceTest::SubObjectList);
				CHECK(items.Size() == 2);

				CHECK(Resources::GetStorage(items[0])->prototype == Resources::GetStorage(subobject1));
				CHECK(items[1] == subobject3);
			}
		}

		ResourceShutdown();
	}
#endif

	TEST_CASE("Resource::SubObjectListPrototypePropagation")
	{
		ResourceInit();
		{
			RegisterTestTypes();

			auto makeSub = [](i64 marker) -> RID
			{
				RID rid = Resources::Create<ResourceTest>();
				ResourceObject write = Resources::Write(rid);
				write.SetInt(ResourceTest::IntValue, marker);
				write.Commit();
				return rid;
			};

			auto verifyMirror = [&](RID instance, const Array<RID>& expectedProtos)
			{
				ResourceObject read = Resources::Read(instance);
				Array<RID>     list = read.GetSubObjectList(ResourceTest::SubObjectList);

				REQUIRE(list.Size() == expectedProtos.Size());

				HashSet<RID> seen;
				for (RID sub : list)
				{
					RID proto = Resources::GetPrototype(sub);
					REQUIRE(proto);

					ResourceObject subRead = Resources::Read(sub);
					ResourceObject protoRead = Resources::Read(proto);
					CHECK(subRead.GetInt(ResourceTest::IntValue) == protoRead.GetInt(ResourceTest::IntValue));

					seen.Insert(proto);
				}

				CHECK(seen.Size() == expectedProtos.Size());
				for (RID expected : expectedProtos)
				{
					CHECK(seen.Has(expected));
				}
			};

			RID prototype = Resources::Create<ResourceTest>();
			RID sub1 = makeSub(1);
			RID sub2 = makeSub(2);

			{
				ResourceObject write = Resources::Write(prototype);
				write.SetString(ResourceTest::StringValue, "prototype");
				write.AddToSubObjectList(ResourceTest::SubObjectList, sub1);
				write.AddToSubObjectList(ResourceTest::SubObjectList, sub2);
				write.Commit();
			}

			RID instance1 = Resources::CreateFromPrototype(prototype);
			RID instance2 = Resources::CreateFromPrototype(prototype);
			RID instance3 = Resources::CreateFromPrototype(prototype);

			verifyMirror(instance1, {sub1, sub2});
			verifyMirror(instance2, {sub1, sub2});
			verifyMirror(instance3, {sub1, sub2});

			RID sub3 = makeSub(3);
			RID sub4 = makeSub(4);

			{
				ResourceObject write = Resources::Write(prototype);
				write.AddToSubObjectList(ResourceTest::SubObjectList, sub3);
				write.AddToSubObjectList(ResourceTest::SubObjectList, sub4);
				write.RemoveFromSubObjectList(ResourceTest::SubObjectList, sub1);
				write.Commit();
			}

			verifyMirror(instance1, {sub2, sub3, sub4});
			verifyMirror(instance2, {sub2, sub3, sub4});
			verifyMirror(instance3, {sub2, sub3, sub4});

			RID sub5 = makeSub(5);

			{
				ResourceObject write = Resources::Write(prototype);
				write.RemoveFromSubObjectList(ResourceTest::SubObjectList, sub2);
				write.AddToSubObjectList(ResourceTest::SubObjectList, sub5);
				write.Commit();
			}

			verifyMirror(instance1, {sub3, sub4, sub5});
			verifyMirror(instance2, {sub3, sub4, sub5});
			verifyMirror(instance3, {sub3, sub4, sub5});

			{
				ResourceObject read = Resources::Read(prototype);
				HashSet<RID>   protoSet = ToHashSet<RID>(read.GetSubObjectList(ResourceTest::SubObjectList));
				CHECK(protoSet.Size() == 3);
				CHECK(protoSet.Has(sub3));
				CHECK(protoSet.Has(sub4));
				CHECK(protoSet.Has(sub5));
			}
		}
		ResourceShutdown();
	}

	TEST_CASE("Resource::DuplicateReference")
	{
		ResourceInit();
		RegisterTestTypes();
		{
			RID ridParent = Resources::Create<ResourceTest>();
			RID ridChild = Resources::Create<ResourceTest>();
			RID ridRef = Resources::Create<ResourceTest>(UUID::RandomUUID());

			{

				ResourceObject refObj = Resources::Write(ridRef);
				refObj.SetInt(ResourceTest::IntValue, 10);
				refObj.Commit();

				ResourceObject objChild = Resources::Write(ridChild);
				objChild.SetReference(ResourceTest::Reference, ridRef);
				objChild.Commit();

				ResourceObject objParent = Resources::Write(ridParent);
				objParent.AddToSubObjectList(ResourceTest::SubObjectList, ridChild);
				objParent.AddToSubObjectList(ResourceTest::SubObjectList, ridRef);
				objParent.Commit();
			}

			{
				RID clone = Resources::Clone(ridParent);
				ResourceObject objParentClone = Resources::Write(clone);
				CHECK(objParentClone.GetSubObjectListCount(ResourceTest::SubObjectList) == 2);

				Span<RID> items =  objParentClone.GetSubObjectList(ResourceTest::SubObjectList);
				CHECK(items[0] != ridChild);
				CHECK(items[1] != ridRef);

				ResourceObject objChild = Resources::Write(items[0]);
				RID ref = objChild.GetReference(ResourceTest::Reference);
				REQUIRE(ref);
				CHECK(ref != ridRef);
			}

			{
				RID prototype = Resources::CreateFromPrototype(ridParent);
				ResourceObject objParentPrototype = Resources::Write(prototype);
				CHECK(objParentPrototype.GetSubObjectListCount(ResourceTest::SubObjectList) == 2);

				Span<RID> items =  objParentPrototype.GetSubObjectList(ResourceTest::SubObjectList);
				CHECK(items[0] != ridChild);
				CHECK(items[1] != ridRef);

				ResourceObject objChild = Resources::Write(items[0]);
				RID ref = objChild.GetReference(ResourceTest::Reference);
				REQUIRE(ref);
				CHECK(ref != ridRef);

				ResourceObject refObj = Resources::Read(ref);
				CHECK(refObj.GetUInt(ResourceTest::IntValue) == 10);
				CHECK(refObj.GetUUID());
			}
		}
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
					RID            subobject = Resources::Create<ResourceTest>(uuids[i + 1]);
					ResourceObject subObjectWrite = Resources::Write(subobject);
					subObjectWrite.SetInt(ResourceTest::IntValue, i);
					subObjectWrite.Commit();

					write.AddToSubObjectList(ResourceTest::SubObjectList, subobject);

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
			RID               newResource = Resources::Deserialize(reader);
			CHECK(newResource);

			ResourceObject read = Resources::Read(newResource);
			CHECK(read.GetUUID() == uuids[0]);
			CHECK(read.GetInt(ResourceTest::IntValue) == 33);
			CHECK(read.GetString(ResourceTest::StringValue) == "44");

			Array<RID> subobjects = read.GetSubObjectList(ResourceTest::SubObjectList);
			CHECK(subobjects.Size() == 5);

			for (u32 i = 0; i < subobjects.Size(); ++i)
			{
				if (auto it = uuidToIndex.Find(i))
				{
					RID            rid = Resources::FindByUUID(it->second);
					ResourceObject subRead = Resources::Read(rid);
					CHECK(subRead.GetUUID() == uuids[i + 1]);
					CHECK(subRead.GetInt(ResourceTest::IntValue) == i);
				}
			}

			ResourceShutdown();
		}
	}

	TEST_CASE("Resource::TypeExportImportJson")
	{
		String exportedJson;

		ResourceInit();
		{
			Resources::Type<ResourceTypeExportImport>("ResourceTypeExportImport")
				.Field<ResourceTypeExportImport::Name>(ResourceFieldType::String)
				.Field<ResourceTypeExportImport::Count>(ResourceFieldType::Int)
				.Field<ResourceTypeExportImport::Ref>(ResourceFieldType::Reference, TypeInfo<ResourceTypeExportImport>::ID())
				.Build();

			ResourceType* resourceType = Resources::FindTypeByName("ResourceTypeExportImport");
			REQUIRE(resourceType != nullptr);
			CHECK(resourceType->GetVersion() == 1);

			JsonArchiveWriter writer;
			Resources::SerializeTypes(writer);
			exportedJson = writer.EmitAsString();
			CHECK(!exportedJson.Empty());

			JsonArchiveReader importReader(exportedJson);
			u32 importedCount = Resources::DeserializeTypes(importReader);
			CHECK(importedCount == 0);

			ResourceType* stillSameType = Resources::FindTypeByName("ResourceTypeExportImport");
			REQUIRE(stillSameType != nullptr);
			CHECK(stillSameType->GetVersion() == 1);
		}
		ResourceShutdown();

		ResourceInit();
		{
			CHECK(Resources::FindTypeByName("ResourceTypeExportImport") == nullptr);

			JsonArchiveReader importReader(exportedJson);
			u32 importedCount = Resources::DeserializeTypes(importReader);
			CHECK(importedCount >= 1);

			ResourceType* importedType = Resources::FindTypeByName("ResourceTypeExportImport");
			REQUIRE(importedType != nullptr);
			CHECK(importedType->GetVersion() == 1);

			ResourceField* nameField = importedType->FindFieldByName("Name");
			REQUIRE(nameField != nullptr);
			CHECK(nameField->GetIndex() == ResourceTypeExportImport::Name);
			CHECK(nameField->GetType() == ResourceFieldType::String);

			ResourceField* refField = importedType->FindFieldByName("Ref");
			REQUIRE(refField != nullptr);
			CHECK(refField->GetType() == ResourceFieldType::Reference);
			CHECK(refField->GetSubType() == TypeInfo<ResourceTypeExportImport>::ID());

			JsonArchiveReader importAgainReader(exportedJson);
			u32 importedAgain = Resources::DeserializeTypes(importAgainReader);
			CHECK(importedAgain == 0);
		}
		ResourceShutdown();
	}

	// Hot Reload Test Structures
	struct HotReloadTestV1
	{
		enum { Value1, Value2 };
	};

	struct HotReloadTestV2
	{
		enum { Value1, Value2, Value3 };
	};

	TEST_CASE("Resource::HotReload::TypeVersioning")
	{
		ResourceInit();

		// Register initial type
		Resources::Type<HotReloadTestV1>("HotReloadTest")
			.Field<HotReloadTestV1::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV1::Value2>(ResourceFieldType::String)
			.Build();

		ResourceType* type1 = Resources::FindTypeByName("HotReloadTest");
		REQUIRE(type1 != nullptr);
		CHECK(type1->GetVersion() == 1);

		// Simulate re-registration (like DLL reload)
		Resources::Type<HotReloadTestV2>("HotReloadTest")
			.Field<HotReloadTestV2::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV2::Value2>(ResourceFieldType::String)
			.Field<HotReloadTestV2::Value3>(ResourceFieldType::Float)
			.Build();

		ResourceType* type2 = Resources::FindTypeByName("HotReloadTest");
		REQUIRE(type2 != nullptr);
		CHECK(type2->GetVersion() == 2);
		CHECK(type1 != type2);

		// Verify fields
		CHECK(type2->GetFields().Size() == 3);
		CHECK(type2->FindFieldByName("Value1") != nullptr);
		CHECK(type2->FindFieldByName("Value2") != nullptr);
		CHECK(type2->FindFieldByName("Value3") != nullptr);

		ResourceShutdown();
	}

	TEST_CASE("Resource::HotReload::InstanceMigration")
	{
		ResourceInit();

		// Register type and create resource
		Resources::Type<HotReloadTestV1>("HotReloadMigration")
			.Field<HotReloadTestV1::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV1::Value2>(ResourceFieldType::String)
			.Build();

		ResourceType* type1 = Resources::FindTypeByName("HotReloadMigration");
		REQUIRE(type1 != nullptr);

		RID rid = Resources::Create(type1->GetID(), UUID::RandomUUID());
		{
			ResourceObject write = Resources::Write(rid);
			write.SetInt(HotReloadTestV1::Value1, 42);
			write.SetString(HotReloadTestV1::Value2, "test_string");
			write.Commit();
		}

		// Verify initial data
		{
			ResourceObject read = Resources::Read(rid);
			CHECK(read.GetInt(HotReloadTestV1::Value1) == 42);
			CHECK(read.GetString(HotReloadTestV1::Value2) == "test_string");
		}

		// Get storage to check version before migration
		ResourceStorage* storage = Resources::GetStorage(rid);
		CHECK(storage->resourceTypeVersion == 1);

		// Simulate type update with new field
		Resources::Type<HotReloadTestV2>("HotReloadMigration")
			.Field<HotReloadTestV2::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV2::Value2>(ResourceFieldType::String)
			.Field<HotReloadTestV2::Value3>(ResourceFieldType::Float)
			.Build();

		ResourceType* type2 = Resources::FindTypeByName("HotReloadMigration");
		REQUIRE(type2 != nullptr);
		CHECK(type2->GetVersion() == 2);

		// Manually trigger migration (simulating what DoReflectionUpdated does)
		Resources::MigrateResources(type1, type2);

		// Verify storage was updated
		CHECK(storage->resourceType == type2);
		CHECK(storage->resourceTypeVersion == 2);

		// Verify data preserved after migration
		{
			ResourceObject read = Resources::Read(rid);
			CHECK(read.GetInt(HotReloadTestV2::Value1) == 42);
			CHECK(read.GetString(HotReloadTestV2::Value2) == "test_string");
			// New field should not have a value set
			CHECK(read.HasValue(HotReloadTestV2::Value3) == false);
		}

		ResourceShutdown();
	}

	TEST_CASE("Resource::HotReload::FieldRemoval")
	{
		ResourceInit();

		// Start with V2 (3 fields)
		Resources::Type<HotReloadTestV2>("HotReloadRemoval")
			.Field<HotReloadTestV2::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV2::Value2>(ResourceFieldType::String)
			.Field<HotReloadTestV2::Value3>(ResourceFieldType::Float)
			.Build();

		ResourceType* type1 = Resources::FindTypeByName("HotReloadRemoval");
		REQUIRE(type1 != nullptr);

		RID rid = Resources::Create(type1->GetID(), UUID::RandomUUID());
		{
			ResourceObject write = Resources::Write(rid);
			write.SetInt(HotReloadTestV2::Value1, 100);
			write.SetString(HotReloadTestV2::Value2, "hello");
			write.SetFloat(HotReloadTestV2::Value3, 3.14f);
			write.Commit();
		}

		// Now register V1 (2 fields - field removed)
		Resources::Type<HotReloadTestV1>("HotReloadRemoval")
			.Field<HotReloadTestV1::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV1::Value2>(ResourceFieldType::String)
			.Build();

		ResourceType* type2 = Resources::FindTypeByName("HotReloadRemoval");
		REQUIRE(type2 != nullptr);
		CHECK(type2->GetVersion() == 2);

		// Manually trigger migration
		Resources::MigrateResources(type1, type2);

		// Verify remaining fields preserved
		{
			ResourceObject read = Resources::Read(rid);
			CHECK(read.GetInt(HotReloadTestV1::Value1) == 100);
			CHECK(read.GetString(HotReloadTestV1::Value2) == "hello");
		}

		ResourceShutdown();
	}

	TEST_CASE("Resource::HotReload::MultipleResources")
	{
		ResourceInit();

		// Register type
		Resources::Type<HotReloadTestV1>("HotReloadMultiple")
			.Field<HotReloadTestV1::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV1::Value2>(ResourceFieldType::String)
			.Build();

		ResourceType* type1 = Resources::FindTypeByName("HotReloadMultiple");
		REQUIRE(type1 != nullptr);

		// Create multiple resources
		Array<RID> resources;
		for (int i = 0; i < 5; ++i)
		{
			RID rid = Resources::Create(type1->GetID(), UUID::RandomUUID());
			{
				ResourceObject write = Resources::Write(rid);
				write.SetInt(HotReloadTestV1::Value1, i * 10);
				write.SetString(HotReloadTestV1::Value2, String("resource_") + std::to_string(i).c_str());
				write.Commit();
			}
			resources.EmplaceBack(rid);
		}

		// Register updated type
		Resources::Type<HotReloadTestV2>("HotReloadMultiple")
			.Field<HotReloadTestV2::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV2::Value2>(ResourceFieldType::String)
			.Field<HotReloadTestV2::Value3>(ResourceFieldType::Float)
			.Build();

		ResourceType* type2 = Resources::FindTypeByName("HotReloadMultiple");

		// Migrate all
		Resources::MigrateResources(type1, type2);

		// Verify all resources migrated correctly
		for (int i = 0; i < 5; ++i)
		{
			ResourceObject read = Resources::Read(resources[i]);
			CHECK(read.GetInt(HotReloadTestV2::Value1) == i * 10);
			CHECK(String(read.GetString(HotReloadTestV2::Value2)) == String("resource_") + std::to_string(i).c_str());
			CHECK(read.HasValue(HotReloadTestV2::Value3) == false);

			ResourceStorage* storage = Resources::GetStorage(resources[i]);
			CHECK(storage->resourceType == type2);
			CHECK(storage->resourceTypeVersion == 2);
		}

		ResourceShutdown();
	}

	TEST_CASE("Resource::HotReload::NoInstanceData")
	{
		ResourceInit();

		// Register type
		Resources::Type<HotReloadTestV1>("HotReloadNoData")
			.Field<HotReloadTestV1::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV1::Value2>(ResourceFieldType::String)
			.Build();

		ResourceType* type1 = Resources::FindTypeByName("HotReloadNoData");
		REQUIRE(type1 != nullptr);

		// Create resource but don't write any data
		RID rid = Resources::Create(type1->GetID(), UUID::RandomUUID());

		// Register updated type
		Resources::Type<HotReloadTestV2>("HotReloadNoData")
			.Field<HotReloadTestV2::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV2::Value2>(ResourceFieldType::String)
			.Field<HotReloadTestV2::Value3>(ResourceFieldType::Float)
			.Build();

		ResourceType* type2 = Resources::FindTypeByName("HotReloadNoData");

		// Migrate
		Resources::MigrateResources(type1, type2);

		// Verify type updated
		ResourceStorage* storage = Resources::GetStorage(rid);
		CHECK(storage->resourceType == type2);
		CHECK(storage->resourceTypeVersion == 2);

		ResourceShutdown();
	}

	TEST_CASE("Resource::HotReload::MigrateResourcesForReflection")
	{
		ResourceInit();

		// Register type v1
		Resources::Type<HotReloadTestV1>("NonReflectHotReload")
			.Field<HotReloadTestV1::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV1::Value2>(ResourceFieldType::String)
			.Build();

		ResourceType* type1 = Resources::FindTypeByName("NonReflectHotReload");
		REQUIRE(type1 != nullptr);
		CHECK(type1->GetVersion() == 1);

		// Create resource
		RID rid = Resources::Create(type1->GetID(), UUID::RandomUUID());
		{
			ResourceObject write = Resources::Write(rid);
			write.SetInt(HotReloadTestV1::Value1, 100);
			write.SetString(HotReloadTestV1::Value2, "hello");
			write.Commit();
		}

		ResourceStorage* storage = Resources::GetStorage(rid);
		CHECK(storage->resourceType == type1);

		// Register type v2 with SAME TypeID (simulating plugin re-registration of same type)
		Resources::Type(type1->GetID(), "NonReflectHotReload")
			.Field<HotReloadTestV2::Value1>(ResourceFieldType::Int)
			.Field<HotReloadTestV2::Value2>(ResourceFieldType::String)
			.Field<HotReloadTestV2::Value3>(ResourceFieldType::Float)
			.Build();

		ResourceType* type2 = Resources::FindTypeByName("NonReflectHotReload");
		REQUIRE(type2 != nullptr);
		CHECK(type2->GetVersion() == 2);

		// Call MigrateResourcesForReflection - should also migrate non-reflection types
		Resources::MigrateResourcesForReflection();

		// Verify resource was migrated
		CHECK(storage->resourceType == type2);
		CHECK(storage->resourceTypeVersion == 2);

		// Verify data preserved
		{
			ResourceObject read = Resources::Read(rid);
			CHECK(read.GetInt(HotReloadTestV2::Value1) == 100);
			CHECK(read.GetString(HotReloadTestV2::Value2) == "hello");
			CHECK(read.HasValue(HotReloadTestV2::Value3) == false);
		}

		ResourceShutdown();
	}
}
