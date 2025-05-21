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

#include "doctest.h"
#include "Skore/App.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Serialization.hpp"

using namespace Skore;

namespace
{
	// Test data - used by both writer and reader functions
	struct TestVector3
	{
		f32 x = 1.5f;
		f32 y = 2.5f;
		f32 z = 3.5f;

		static void RegisterType(NativeReflectType<TestVector3>& type)
		{
			type.Field<&TestVector3::x>("x");
			type.Field<&TestVector3::y>("y");
			type.Field<&TestVector3::z>("z");
		}

		void Clear()
		{
			x = 0.0f;
			y = 0.0f;
			z = 0.0f;
		}
	};

	enum class TestEntityState
	{
		None,
		Alive,
		Dead,
		OnHold
	};


	struct TestEntity
	{
		String name = "TestEntity";
		i32 id = 42;
		bool active = true;
		TestVector3 position;
		TestVector3 rotation;
		Array<String> tags{"primary", "dynamic", "renderable"};
		Array<TestVector3> other = {TestVector3{1.0, 2.0, 3.0}, TestVector3{3.0, 4.0, 5.0}, TestVector3{5.0, 6.0, 7.0}};
		TestEntityState state = TestEntityState::OnHold;

		static void RegisterType(NativeReflectType<TestEntity>& type)
		{
			type.Field<&TestEntity::name>("name");
			type.Field<&TestEntity::id>("id");
			type.Field<&TestEntity::active>("active");
			type.Field<&TestEntity::position>("position");
			type.Field<&TestEntity::rotation>("rotation");
			type.Field<&TestEntity::tags>("tags");
			type.Field<&TestEntity::other>("other");
			type.Field<&TestEntity::state>("state");
		}

		void Clear()
		{
			name = "";
			id = 0;
			active = false;
			position.Clear();
			rotation.Clear();
			tags.Clear();
			tags.ShrinkToFit();
			other.Clear();
			other.ShrinkToFit();
			state = {};
		}
	};

	void WriteArchiveData(ArchiveWriter& writer)
	{
		// Write primitive types
		writer.WriteBool("boolValue", true);
		writer.WriteInt("intValue", -123456789);
		writer.WriteUInt("uintValue", 987654321);
		writer.WriteFloat("floatValue", 3.14159265359);
		writer.WriteString("stringValue", "Hello, Archive!");

		// Write a nested object (vector)
		writer.BeginMap("vector3");
		writer.WriteFloat("x", 1.5);
		writer.WriteFloat("y", 2.5);
		writer.WriteFloat("z", 3.5);
		writer.EndMap();

		// Write a sequence of values
		writer.BeginSeq("intArray");
		writer.AddInt(1);
		writer.AddInt(2);
		writer.AddInt(3);
		writer.AddInt(4);
		writer.AddInt(5);
		writer.EndSeq();

		// Write a sequence of objects
		writer.BeginSeq("entities");
		
		// First entity
		writer.BeginMap();
		writer.WriteString("name", "Entity1");
		writer.WriteInt("id", 1);
		writer.WriteBool("active", true);
		
		// Position vector within first entity
		writer.BeginMap("position");
		writer.WriteFloat("x", 10.0);
		writer.WriteFloat("y", 20.0);
		writer.WriteFloat("z", 30.0);
		writer.EndMap();
		
		// Tags array within first entity
		writer.BeginSeq("tags");
		writer.AddString("player");
		writer.AddString("enemy");
		writer.EndSeq();
		
		writer.EndMap(); // End first entity
		
		// Second entity
		writer.BeginMap();
		writer.WriteString("name", "Entity2");
		writer.WriteInt("id", 2);
		writer.WriteBool("active", false);
		
		// Position vector within second entity
		writer.BeginMap("position");
		writer.WriteFloat("x", -10.0);
		writer.WriteFloat("y", -20.0);
		writer.WriteFloat("z", -30.0);
		writer.EndMap();
		
		// Tags array within second entity
		writer.BeginSeq("tags");
		writer.AddString("static");
		writer.AddString("obstacle");
		writer.EndSeq();
		
		writer.EndMap(); // End second entity
		
		writer.EndSeq(); // End entities sequence

		// Write a complex nested structure
		writer.BeginMap("gameState");
		writer.WriteString("level", "level1");
		writer.WriteInt("score", 9000);
		writer.WriteBool("paused", false);
		
		// Players array in game state
		writer.BeginSeq("players");
		
		// First player
		writer.BeginMap();
		writer.WriteString("name", "Player1");
		writer.WriteInt("health", 100);
		writer.WriteFloat("speed", 5.5);
		
		// Inventory array in first player
		writer.BeginSeq("inventory");
		writer.BeginMap();
		writer.WriteString("item", "Sword");
		writer.WriteInt("count", 1);
		writer.EndMap();
		
		writer.BeginMap();
		writer.WriteString("item", "Potion");
		writer.WriteInt("count", 5);
		writer.EndMap();
		writer.EndSeq(); // End inventory
		
		writer.EndMap(); // End first player
		
		// Second player
		writer.BeginMap();
		writer.WriteString("name", "Player2");
		writer.WriteInt("health", 85);
		writer.WriteFloat("speed", 6.0);
		
		// Inventory array in second player
		writer.BeginSeq("inventory");
		writer.BeginMap();
		writer.WriteString("item", "Bow");
		writer.WriteInt("count", 1);
		writer.EndMap();
		
		writer.BeginMap();
		writer.WriteString("item", "Arrow");
		writer.WriteInt("count", 30);
		writer.EndMap();
		writer.EndSeq(); // End inventory
		
		writer.EndMap(); // End second player
		
		writer.EndSeq(); // End players array
		writer.EndMap(); // End gameState
	}

	void CompareReaderData(ArchiveReader& reader)
	{
		// Read and verify primitive types
		CHECK(reader.ReadBool("boolValue") == true);
		CHECK(reader.ReadInt("intValue") == -123456789);
		CHECK(reader.ReadUInt("uintValue") == 987654321);
		CHECK(reader.ReadFloat("floatValue") == doctest::Approx(3.14159265359));
		CHECK(reader.ReadString("stringValue") == "Hello, Archive!");

		// Read and verify a nested object (vector)
		reader.BeginMap("vector3");
		{
			CHECK(reader.ReadFloat("x") == doctest::Approx(1.5));
			CHECK(reader.ReadFloat("y") == doctest::Approx(2.5));
			CHECK(reader.ReadFloat("z") == doctest::Approx(3.5));
		}
		reader.EndMap();

		// Read and verify a sequence of values
		reader.BeginSeq("intArray");
		{
			CHECK(reader.NextSeqEntry());
			CHECK(reader.GetInt() == 1);
			CHECK(reader.NextSeqEntry());
			CHECK(reader.GetInt() == 2);
			CHECK(reader.NextSeqEntry());
			CHECK(reader.GetInt() == 3);
			CHECK(reader.NextSeqEntry());
			CHECK(reader.GetInt() == 4);
			CHECK(reader.NextSeqEntry());
			CHECK(reader.GetInt() == 5);
			CHECK_FALSE(reader.NextSeqEntry());
		}
		reader.EndSeq();

		// Read and verify a sequence of objects
		reader.BeginSeq("entities");
		CHECK(reader.NextSeqEntry());
		{
			reader.BeginMap(); 	// Verify first entity
			{
				CHECK(reader.ReadString("name") == "Entity1");
				CHECK(reader.ReadInt("id") == 1);
				CHECK(reader.ReadBool("active") == true);

				// Position vector within first entity
				reader.BeginMap("position");
				{
					CHECK(reader.ReadFloat("x") == doctest::Approx(10.0));
					CHECK(reader.ReadFloat("y") == doctest::Approx(20.0));
					CHECK(reader.ReadFloat("z") == doctest::Approx(30.0));
				}
				reader.EndMap();

				reader.BeginSeq("tags");
				{
					CHECK(reader.NextSeqEntry());
					CHECK(reader.GetString() == "player");
					CHECK(reader.NextSeqEntry());
					CHECK(reader.GetString() == "enemy");
					CHECK_FALSE(reader.NextSeqEntry());
				}
				reader.EndSeq();
			}
			reader.EndMap(); // End first entity
		}
		CHECK(reader.NextSeqEntry()); // Verify second entity
		{
			reader.BeginMap();
			String a = reader.ReadString("name");
			CHECK(reader.ReadString("name") == "Entity2");
			CHECK(reader.ReadInt("id") == 2);
			CHECK(reader.ReadBool("active") == false);

			// Position vector within second entity
			reader.BeginMap("position");
			CHECK(reader.ReadFloat("x") == doctest::Approx(-10.0));
			CHECK(reader.ReadFloat("y") == doctest::Approx(-20.0));
			CHECK(reader.ReadFloat("z") == doctest::Approx(-30.0));
			reader.EndMap();

			// Tags array within second entity
			reader.BeginSeq("tags");
			CHECK(reader.NextSeqEntry());
			CHECK(reader.GetString() == "static");
			CHECK(reader.NextSeqEntry());
			CHECK(reader.GetString() == "obstacle");
			CHECK_FALSE(reader.NextSeqEntry());
			reader.EndSeq();

			reader.EndMap(); // End second entity
		}

		CHECK_FALSE(reader.NextSeqEntry());
		reader.EndSeq(); // End entities sequence

		// Read and verify complex nested structure
		reader.BeginMap("gameState");
		CHECK(reader.ReadString("level") == "level1");
		CHECK(reader.ReadInt("score") == 9000);
		CHECK(reader.ReadBool("paused") == false);
		
		// Players array in game state
		reader.BeginSeq("players");
		
		// Verify first player
		CHECK(reader.NextSeqEntry());
		reader.BeginMap();
		CHECK(reader.ReadString("name") == "Player1");
		CHECK(reader.ReadInt("health") == 100);
		CHECK(reader.ReadFloat("speed") == doctest::Approx(5.5));
		
		// Inventory array in first player
		reader.BeginSeq("inventory");
		CHECK(reader.NextSeqEntry());
		reader.BeginMap();
		CHECK(reader.ReadString("item") == "Sword");
		CHECK(reader.ReadInt("count") == 1);
		reader.EndMap();
		
		CHECK(reader.NextSeqEntry());
		reader.BeginMap();
		CHECK(reader.ReadString("item") == "Potion");
		CHECK(reader.ReadInt("count") == 5);
		reader.EndMap();
		CHECK_FALSE(reader.NextSeqEntry());
		reader.EndSeq(); // End inventory
		
		reader.EndMap(); // End first player
		
		// Verify second player
		CHECK(reader.NextSeqEntry());
		reader.BeginMap();
		CHECK(reader.ReadString("name") == "Player2");
		CHECK(reader.ReadInt("health") == 85);
		CHECK(reader.ReadFloat("speed") == doctest::Approx(6.0));
		
		// Inventory array in second player
		reader.BeginSeq("inventory");
		CHECK(reader.NextSeqEntry());
		reader.BeginMap();
		CHECK(reader.ReadString("item") == "Bow");
		CHECK(reader.ReadInt("count") == 1);
		reader.EndMap();
		
		CHECK(reader.NextSeqEntry());
		reader.BeginMap();
		CHECK(reader.ReadString("item") == "Arrow");
		CHECK(reader.ReadInt("count") == 30);
		reader.EndMap();
		CHECK_FALSE(reader.NextSeqEntry());
		reader.EndSeq(); // End inventory
		
		reader.EndMap(); // End second player
		
		CHECK_FALSE(reader.NextSeqEntry());
		reader.EndSeq(); // End players array
		reader.EndMap(); // End gameState
	}

	void WriteMapNavigationTestData(ArchiveWriter& writer)
	{
		// Write a simple map structure to test map navigation
		writer.BeginMap("testMap");
		writer.WriteString("stringKey", "StringValue");
		writer.WriteInt("intKey", 12345);
		writer.WriteBool("boolKey", true);
		writer.WriteFloat("floatKey", 3.14);
		
		// Nested map
		writer.BeginMap("nestedMap");
		writer.WriteString("innerString", "InnerValue");
		writer.WriteInt("innerInt", 67890);
		writer.EndMap();
		
		// Map with sequence
		writer.BeginSeq("mapWithSeq");
		writer.AddString("Item1");
		writer.AddString("Item2");
		writer.AddString("Item3");
		writer.EndSeq();
		
		writer.EndMap(); // End testMap
	}

	void TestMapNavigation(ArchiveReader& reader)
	{
		// Test map navigation with NextMapEntry and GetCurrentKey
		reader.BeginMap("testMap");
		
		// Create maps to track what we've seen
		HashMap<String, bool> foundKeys;
		HashMap<String, String> expectedValueTypes;
		
		// Set up expected keys and value types
		expectedValueTypes["stringKey"] = "string";
		expectedValueTypes["intKey"] = "int";
		expectedValueTypes["boolKey"] = "bool";
		expectedValueTypes["floatKey"] = "float";
		expectedValueTypes["nestedMap"] = "map";
		expectedValueTypes["mapWithSeq"] = "seq";
		
		// Track how many entries we've seen
		int entryCount = 0;
		
		// Navigate through the map entries
		while (reader.NextMapEntry())
		{
			entryCount++;
			
			// Get current key and mark it as found
			StringView key = reader.GetCurrentKey();
			String keyStr(key.begin(), key.Size());
			foundKeys[keyStr] = true;
			
			// Check that the key is one we expect
			CHECK(expectedValueTypes.Find(keyStr) != expectedValueTypes.end());
			
			String valueType = expectedValueTypes[keyStr];
			
			// Now read the appropriate type based on the key
			if (valueType == "string")
			{
				StringView value = reader.GetString();
				CHECK(value == "StringValue");
			}
			else if (valueType == "int")
			{
				i64 value = reader.GetInt();
				CHECK(value == 12345);
			}
			else if (valueType == "bool")
			{
				bool value = reader.GetBool();
				CHECK(value == true);
			}
			else if (valueType == "float")
			{
				f64 value = reader.GetFloat();
				CHECK(value == doctest::Approx(3.14));
			}
			else if (valueType == "map")
			{
				// Test nested map navigation
				reader.BeginMap();
				
				// Track nested entries
				HashMap<String, bool> foundNestedKeys;
				int nestedEntryCount = 0;
				
				while (reader.NextMapEntry())
				{
					nestedEntryCount++;
					StringView nestedKey = reader.GetCurrentKey();
					String nestedKeyStr(nestedKey.begin(), nestedKey.Size());
					foundNestedKeys[nestedKeyStr] = true;
					
					if (nestedKeyStr == "innerString")
					{
						CHECK(reader.GetString() == "InnerValue");
					}
					else if (nestedKeyStr == "innerInt")
					{
						CHECK(reader.GetInt() == 67890);
					}
				}
				
				// Verify we found all nested keys
				CHECK(nestedEntryCount == 2);
				CHECK(foundNestedKeys["innerString"]);
				CHECK(foundNestedKeys["innerInt"]);
				
				reader.EndMap();
			}
			else if (valueType == "seq")
			{
				// Test sequence inside a map
				reader.BeginSeq();
				
				CHECK(reader.NextSeqEntry());
				CHECK(reader.GetString() == "Item1");
				
				CHECK(reader.NextSeqEntry());
				CHECK(reader.GetString() == "Item2");
				
				CHECK(reader.NextSeqEntry());
				CHECK(reader.GetString() == "Item3");
				
				CHECK_FALSE(reader.NextSeqEntry());
				
				reader.EndSeq();
			}
		}
		
		// Check that we found all entries
		CHECK(entryCount == 6);
		CHECK(foundKeys["stringKey"]);
		CHECK(foundKeys["intKey"]);
		CHECK(foundKeys["boolKey"]);
		CHECK(foundKeys["floatKey"]);
		CHECK(foundKeys["nestedMap"]);
		CHECK(foundKeys["mapWithSeq"]);
		
		reader.EndMap();
	}
	
	TEST_CASE("IO::Serialization::Yaml")
	{
		YamlArchiveWriter writer;
		WriteArchiveData(writer);

		String yaml = writer.EmitAsString();

		CHECK(yaml.Size() > 4);

		YamlArchiveReader reader(yaml);
		CompareReaderData(reader);
	}
	
	TEST_CASE("IO::Serialization::YamlMapNavigation")
	{
		YamlArchiveWriter writer;
		WriteMapNavigationTestData(writer);
		
		String yaml = writer.EmitAsString();
		CHECK(yaml.Size() > 4);
		
		YamlArchiveReader reader(yaml);
		TestMapNavigation(reader);
	}

	TEST_CASE("IO::Serialization::BinaryFull")
	{
		BinaryArchiveWriter writer;
		WriteArchiveData(writer);

		Span<u8> data = writer.GetData();
		CHECK(data.Size() > 0);
		BinaryArchiveReader reader(data);
		CompareReaderData(reader);
	}

	TEST_CASE("IO::Serialization::BinaryMapNavigation")
	{
		BinaryArchiveWriter writer;
		WriteMapNavigationTestData(writer);

		Span<u8> data = writer.GetData();
		CHECK(data.Size() > 0);

		BinaryArchiveReader reader(data);
		TestMapNavigation(reader);
	}


	TEST_CASE("IO::Serialization::Binary")
	{
		BinaryArchiveWriter writer;
		writer.WriteBool("testbool", true);

		writer.BeginSeq("seq");
		writer.AddInt(3);
		writer.AddInt(4);
		writer.AddInt(5);
		writer.EndSeq();

		writer.BeginMap("map");
		{
			writer.BeginMap("another-map");
			{
				writer.WriteString("zzzz", "zzzzzzzzzzzzz");
			}
			writer.EndMap();

			writer.WriteString("huh", "huhhuh");
		}
		writer.EndMap();

		writer.WriteString("testString", "blahblahbbasdasd");

		Span<u8> data = writer.GetData();
		CHECK(data.Size() > 0);

		BinaryArchiveReader reader(data);
		CHECK(reader.ReadString("testString") == "blahblahbbasdasd");
		CHECK(reader.ReadBool("testbool"));

		reader.BeginSeq("seq");
		CHECK(reader.NextSeqEntry());
		CHECK(reader.GetInt() == 3);
		CHECK(reader.NextSeqEntry());
		CHECK(reader.GetInt() == 4);
		CHECK(reader.NextSeqEntry());
		CHECK(reader.GetInt() == 5);
		CHECK_FALSE(reader.NextSeqEntry());
		reader.EndSeq();

		reader.BeginMap("map");
		{
			CHECK(reader.ReadString("huh") == "huhhuh");

			reader.BeginMap("another-map");
			{
				CHECK(reader.ReadString("zzzz") == "zzzzzzzzzzzzz");
			}
			reader.EndMap();
		}
		reader.EndMap();
	}

	void TestValues(TestEntity& entity1, TestEntity& entity2)
	{
		// One line per variable comparison
		CHECK(entity1.name == entity2.name);
		CHECK(entity1.id == entity2.id);
		CHECK(entity1.active == entity2.active);
		CHECK(entity1.state == entity2.state);

		// Position
		CHECK(entity1.position.x == doctest::Approx(entity2.position.x));
		CHECK(entity1.position.y == doctest::Approx(entity2.position.y));
		CHECK(entity1.position.z == doctest::Approx(entity2.position.z));

		// Rotation
		CHECK(entity1.rotation.x == doctest::Approx(entity2.rotation.x));
		CHECK(entity1.rotation.y == doctest::Approx(entity2.rotation.y));
		CHECK(entity1.rotation.z == doctest::Approx(entity2.rotation.z));

		// Tags array
		CHECK(entity1.tags.Size() == entity2.tags.Size());
		if (entity1.tags.Size() == entity2.tags.Size())
		{
			for (size_t i = 0; i < entity1.tags.Size(); i++)
			{
				CHECK(entity1.tags[i] == entity2.tags[i]);
			}
		}

		// Tags array
		CHECK(entity1.other.Size() == entity2.other.Size());
		if (entity1.other.Size() == entity2.other.Size())
		{
			for (size_t i = 0; i < entity1.other.Size(); i++)
			{
				CHECK(entity1.other[i].x == doctest::Approx(entity2.other[i].x));
				CHECK(entity1.other[i].y == doctest::Approx(entity2.other[i].y));
				CHECK(entity1.other[i].z == doctest::Approx(entity2.other[i].z));
			}
		}
	}

	TEST_CASE("IO::Serialization::SerializationYaml")
	{
		App::ResetContext();

		{
			Reflection::Type<TestVector3>();
			Reflection::Type<TestEntity>();

			auto testEntityState = Reflection::Type<TestEntityState>();
			testEntityState.Value<TestEntityState::None>();
			testEntityState.Value<TestEntityState::Alive>();
			testEntityState.Value<TestEntityState::Dead>();
			testEntityState.Value<TestEntityState::OnHold>();
		}

		ReflectType* type = Reflection::FindType<TestEntity>();

		TestEntity entity1;
		YamlArchiveWriter writer;
		Serialization::Serialize(type,  writer, &entity1);

		String str = writer.EmitAsString();
		REQUIRE(str.Size() > 4);

		TestEntity entity2;
		entity2.Clear();
		YamlArchiveReader reader(str);
		Serialization::Deserialize(type, reader, &entity2);

		TestValues(entity1, entity2);
	}

	TEST_CASE("IO::Serialization::SerializationBinary")
	{
		App::ResetContext();

		{
			Reflection::Type<TestVector3>();
			Reflection::Type<TestEntity>();

			auto testEntityState = Reflection::Type<TestEntityState>();
			testEntityState.Value<TestEntityState::None>();
			testEntityState.Value<TestEntityState::Alive>();
			testEntityState.Value<TestEntityState::Dead>();
			testEntityState.Value<TestEntityState::OnHold>();
		}

		ReflectType* type = Reflection::FindType<TestEntity>();

		TestEntity entity1;
		BinaryArchiveWriter writer;
		Serialization::Serialize(type,  writer, &entity1);

		Span<u8> vl = writer.GetData();
		REQUIRE(vl.Size() > 0);

		TestEntity entity2;
		entity2.Clear();

		BinaryArchiveReader reader(vl);
		Serialization::Deserialize(type, reader, &entity2);

		TestValues(entity1, entity2);
	}
}