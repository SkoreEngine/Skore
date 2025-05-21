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


#include <string>

#include "Skore/App.hpp"
#include "doctest.h"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/Ref.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/UUID.hpp"

#include <vector>
#include <thread>

#include "Skore/Core/Variant.hpp"

using namespace Skore;

namespace
{
	TEST_CASE("Core::ArrayTestBasics")
	{
		Array<i32> arrInt{};
		CHECK(arrInt.Empty());

		arrInt.Reserve(10);
		arrInt.EmplaceBack(1);
		arrInt.EmplaceBack(2);
		arrInt.EmplaceBack(3);

		CHECK(!arrInt.Empty());
		CHECK(arrInt.Size() == 3);
		CHECK(arrInt.Capacity() == 10);

		CHECK(arrInt.Data() != nullptr);

		CHECK(arrInt[0] == 1);
		CHECK(arrInt[1] == 2);
		CHECK(arrInt[2] == 3);

		i32 i = 0;
		for (const auto it : arrInt)
		{
			CHECK(it == ++i);
		}

		arrInt.PopBack();
		CHECK(arrInt.Size() == 2);

		arrInt.Clear();
		CHECK(arrInt.Empty());
		CHECK(arrInt.Size() == 0);
		CHECK(arrInt.Capacity() == 10);
	}

	TEST_CASE("Core::ArrayTestResize")
	{
		Array<i32> arrInt{};
		arrInt.Resize(10, 5);
		for (int i = 0; i < 10; ++i)
		{
			CHECK(arrInt[i] == 5);
		}
	}

	TEST_CASE("Core::ArrayTestInsertBegin")
	{
		Array<i32> arrInt{};
		arrInt.EmplaceBack(1);
		arrInt.EmplaceBack(2);
		arrInt.EmplaceBack(3);


		{
			Array<i32> arrOther{};
			arrOther.EmplaceBack(10);
			arrOther.EmplaceBack(20);
			arrInt.Insert(arrInt.begin(), arrOther.begin(), arrOther.end());
		}

		REQUIRE(arrInt.Size() == 5);

		CHECK(arrInt[0] == 10);
		CHECK(arrInt[1] == 20);
		CHECK(arrInt[2] == 1);
		CHECK(arrInt[3] == 2);
		CHECK(arrInt[4] == 3);
	}

	TEST_CASE("Core::ArrayTestInsertMiddle")
	{
		Array<i32> arrInt{};
		arrInt.EmplaceBack(1);
		arrInt.EmplaceBack(2);
		arrInt.EmplaceBack(3);


		{
			Array<i32> arrOther{};
			arrOther.EmplaceBack(10);
			arrOther.EmplaceBack(20);
			arrInt.Insert(arrInt.begin() + 2, arrOther.begin(), arrOther.end());
		}

		REQUIRE(arrInt.Size() == 5);

		CHECK(arrInt[0] == 1);
		CHECK(arrInt[1] == 2);
		CHECK(arrInt[2] == 10);
		CHECK(arrInt[3] == 20);
		CHECK(arrInt[4] == 3);
	}

	TEST_CASE("Core::ArrayTestInsertEnd")
	{
		Array<i32> arrInt{};
		arrInt.EmplaceBack(1);
		arrInt.EmplaceBack(2);
		arrInt.EmplaceBack(3);

		{
			Array<i32> arrOther{};
			arrOther.EmplaceBack(10);
			arrOther.EmplaceBack(20);
			arrInt.Insert(arrInt.end(), arrOther.begin(), arrOther.end());
		}

		REQUIRE(arrInt.Size() == 5);

		CHECK(arrInt[0] == 1);
		CHECK(arrInt[1] == 2);
		CHECK(arrInt[2] == 3);
		CHECK(arrInt[3] == 10);
		CHECK(arrInt[4] == 20);
	}

	TEST_CASE("Core::ArrayTestErase")
	{
		Array<i32> arrInt{};
		arrInt.EmplaceBack(1);

		arrInt.EmplaceBack(2);
		arrInt.EmplaceBack(3);
		arrInt.EmplaceBack(4);

		arrInt.EmplaceBack(5);
		arrInt.EmplaceBack(6);

		arrInt.Erase(arrInt.begin() + 1, arrInt.begin() + 4);

		CHECK(arrInt.Size() == 3);

		CHECK(arrInt[0] == 1);
		CHECK(arrInt[1] == 5);
		CHECK(arrInt[2] == 6);
	}

	TEST_CASE("Core::ArrayTestCopy")
	{
		Array<i32> arrInt{};
		arrInt.EmplaceBack(1);
		arrInt.EmplaceBack(2);
		arrInt.EmplaceBack(3);

		Array<i32> copy{arrInt};

		CHECK(copy[0] == 1);
		CHECK(copy[1] == 2);
		CHECK(copy[2] == 3);

		Array<i32> assign = arrInt;
		CHECK(assign[0] == 1);
		CHECK(assign[1] == 2);
		CHECK(assign[2] == 3);
	}

	TEST_CASE("Core::ArrayTestMove")
	{
		Array<i32> arrInt{};
		arrInt.EmplaceBack(1);
		arrInt.EmplaceBack(2);
		arrInt.EmplaceBack(3);

		Array<i32> move = Traits::Move(arrInt);

		CHECK(move[0] == 1);
		CHECK(move[1] == 2);
		CHECK(move[2] == 3);

		CHECK(arrInt.Empty());
	}

	Array<i32> GetArray()
	{
		Array<i32> arrInt{};
		arrInt.EmplaceBack(1);
		arrInt.EmplaceBack(2);
		arrInt.EmplaceBack(3);
		return arrInt;
	}

	TEST_CASE("Core::ArrayTestMoveFunc")
	{
		Array<i32> arr = GetArray();
		CHECK(arr[0] == 1);
		CHECK(arr[1] == 2);
		CHECK(arr[2] == 3);
		CHECK(!arr.Empty());
	}

	TEST_CASE("Core::ArrayTestSwap")
	{
		Array<i32> arr1{};
		arr1.EmplaceBack(1);
		arr1.EmplaceBack(2);

		Array<i32> arr2{};
		arr2.EmplaceBack(3);
		arr2.EmplaceBack(4);

		arr2.Swap(arr1);

		CHECK(arr2[0] == 1);
		CHECK(arr2[1] == 2);

		CHECK(arr1[0] == 3);
		CHECK(arr1[1] == 4);
	}

	TEST_CASE("Core::ArrayTestShrinkToFit")
	{
		{
			Array<i32> arr1{};
			arr1.Reserve(10);

			CHECK(arr1.Capacity() == 10);
			arr1.ShrinkToFit();
			CHECK(arr1.Capacity() == 0);
		}

		{
			Array<i32> arr1{};
			arr1.Reserve(10);
			arr1.EmplaceBack(1);
			arr1.EmplaceBack(2);

			CHECK(arr1.Capacity() == 10);
			arr1.ShrinkToFit();
			CHECK(arr1.Capacity() == 2);
		}
	}

	TEST_CASE("Core::ArrayTestCompare")
	{
		Array<i32> arr1{};
		arr1.EmplaceBack(1);
		arr1.EmplaceBack(2);

		Array<i32> arr2{};
		arr2.EmplaceBack(1);
		arr2.EmplaceBack(2);

		CHECK(arr1 == arr2);

		Array<i32> arr3{};
		arr3.EmplaceBack(2);
		arr3.EmplaceBack(2);

		CHECK(arr1 != arr3);
	}

	TEST_CASE("Core::SpanTestBasics")
	{
		Array<i32> arr1{};
		arr1.EmplaceBack(10);
		arr1.EmplaceBack(20);

		Span<i32> span = arr1;

		CHECK(span.Size() == 2);
		CHECK(span[0] == 10);
		CHECK(span[1] == 20);

		i32 sum = 0;
		for (const i32& vl : span)
		{
			sum += vl;
		}
		CHECK(sum == 30);
	}

	struct TestStruct
	{
		int                      value{};
		HashMap<int, TestStruct> map{};
	};

	TEST_CASE("Core::HashMapTestBasics")
	{
		HashMap<int, int> map{};

		for (int i = 0; i < 1000; ++i)
		{
			map.Insert(MakePair(i, i * 100));
		}

		bool check = true;

		CHECK(map.Has(100));

		for (int i = 0; i < 1000; ++i)
		{
			HashMap<int, int>::Iterator it = map.Find(i);
			if (it == map.end())
			{
				REQUIRE(false);
			}

			if (it->second != i * 100)
			{
				check = false;
			}
		}

		CHECK(check);

		map.Clear();

		HashMap<int, int>::Iterator it = map.Find(1);
		REQUIRE(it == map.end());
	}

	TEST_CASE("Core::HashMapTestStruct")
	{
		TestStruct mapStruct{};
		mapStruct.map.Emplace(10, TestStruct{120});
		CHECK(mapStruct.map[10].value == 120);
	}

	TEST_CASE("Core::HashMapTestForeach")
	{
		HashMap<int, int> map{};
		map.Insert(MakePair(1, 20));
		map.Insert(MakePair(2, 40));

		i32 sum = 0;
		for (HashMap<int, int>::Node& it : map)
		{
			sum += it.second;
		}

		CHECK(sum == 60);
	}

	TEST_CASE("Core::HashMapTestMove")
	{
		HashMap<int, int> map{};
		map.Insert(MakePair(1, 20));
		map.Insert(MakePair(2, 40));

		HashMap<int, int> other{Traits::Move(map)};
		CHECK(other[2] == 40);
		CHECK(map.Empty());
	}

	TEST_CASE("Core::HashMapTestCopy")
	{
		HashMap<int, int> map{};
		map.Insert(MakePair(1, 20));
		map.Insert(MakePair(2, 40));

		HashMap<int, int> other = map;

		CHECK(map[1] == 20);
		CHECK(map[2] == 40);

		CHECK(other[1] == 20);
		CHECK(other[2] == 40);

		CHECK(other.Size() == 2);
	}

	TEST_CASE("Core::HashMapTestErase")
	{
		HashMap<int, int> map{};
		map.Insert(MakePair(1, 20));
		map.Insert(MakePair(2, 40));

		map.Erase(map.Find(1));

		CHECK(map.Find(1) == map.end());
		CHECK(map.Find(2) != map.end());

		map.Erase(2);

		CHECK(map.Find(2) == map.end());
	}

	TEST_CASE("Core::HashMapTestStr")
	{
		HashMap<String, String> map{};
		map["AAAA"] = "BBBB";
		map["CCCC"] = "DDDD";

		for (int i = 0; i < 10000; ++i)
		{
			std::string str = std::to_string(i);
			map.Insert(String{str.c_str()}, String{str.c_str()});
		}

		{
			auto it = map.Find("CCCC");
			REQUIRE(it != map.end());
			CHECK(it->second == "DDDD");
		}

		{
			StringView strView = {"AAAA"};
			auto       it = map.Find(strView);
			REQUIRE(it != map.end());
			CHECK(it->second == "BBBB");
		}
	}

	TEST_CASE("Core::HashMapTestEmplace")
	{
		HashMap<String, String> map{};
		map.Emplace("AAA", "BBB");
		CHECK(map.Has("AAA"));
	}

	TEST_CASE("TestString_Constructor")
	{
		{
			String s;
			CHECK(s.Size() == 0);
		}
		{
			String s("hello");
			CHECK(s.Size() == 5);
			CHECK(0 == strcmp(s.CStr(), "hello"));
		}
		{
			String s("hello world", 5);
			CHECK(s.Size() == 5);
			CHECK(0 == strcmp(s.CStr(), "hello"));
		}
		{
			const String other("hello");
			String       s = other;

			CHECK(s.Size() == 5);
			CHECK(0 == strcmp(s.CStr(), "hello"));
		}
		{
			String other("hello");
			String s = Traits::Move(other);

			CHECK(s.Size() == 5);
			CHECK(0 == strcmp(s.CStr(), "hello"));
			CHECK(other.Size() == 0);
		}
	}

	TEST_CASE("TestString_Assign")
	{
		{
			const String other("hello");
			String       s("new");
			s = other;

			CHECK(s.Size() == 5);
			CHECK(0 == strcmp(s.CStr(), "hello"));
		}
		{
			String other("hello");
			String s("new");
			s = std::move(other);

			CHECK(s.Size() == 5);
			CHECK(0 == strcmp(s.CStr(), "hello"));
			CHECK(other.Size() == 0);
		}
	}

	TEST_CASE("TestString_Empty")
	{
		String s;
		CHECK(s.Empty());
		CHECK(s.Capacity() == 17);
		CHECK(s.begin() == s.end());
		CHECK(strlen(s.CStr()) == 0);
		CHECK(s == "");
	}

	TEST_CASE("TestString_Small")
	{
		String s1("");

		CHECK(s1.Empty());
		CHECK(s1.Capacity() == 17);
		CHECK(s1.begin() == s1.end());
		CHECK(strlen(s1.CStr()) == 0);
		CHECK(s1 == "");

		String s2("hello");
		CHECK(s2.Size() == 5);
		CHECK(s2.Capacity() == 17);
		CHECK(s2.begin() + 5 == s2.end());
		CHECK(strlen(s2.CStr()) == 5);
		CHECK(s2 == "hello");

		String s3("exactly 17 charrr");
		CHECK(s3.Size() == 17);
		CHECK(s3.Capacity() == 17);
		CHECK(s3.begin() + 17 == s3.end());
		CHECK(strlen(s3.CStr()) == 17);
		CHECK(s3 == "exactly 17 charrr");
	}

	TEST_CASE("TestString_Long")
	{
		const char* origin = "very long string larger than large string limit";
		size_t      len = strlen(origin);
		String      s(origin);
		CHECK(s.Size() == len);
		CHECK(s.Capacity() == len);
		CHECK(s.begin() + len == s.end());
		CHECK(strlen(s.CStr()) == len);
		CHECK(s == origin);
	}

	TEST_CASE("TestString_Assign")
	{
		String      s;
		const char* originshort = "short";
		size_t      lenshort = strlen(originshort);
		s = originshort;
		CHECK(s.Size() == lenshort);
		CHECK(s.Capacity() == 17);
		CHECK(s.begin() + lenshort == s.end());
		CHECK(strlen(s.CStr()) == lenshort);
		CHECK(s == originshort);

		const char* originlong = "long long long long long long long long long long long long";
		size_t      lenlong = strlen(originlong);
		s = originlong;
		CHECK(s.Size() == lenlong);
		CHECK(s.Capacity() == lenlong);
		CHECK(s.begin() + lenlong == s.end());
		CHECK(strlen(s.CStr()) == lenlong);
		CHECK(s == originlong);

		s = originshort;
		CHECK(s.Size() == lenshort);
		CHECK(s.Capacity() == lenlong);
		CHECK(s.begin() + lenshort == s.end());
		CHECK(strlen(s.CStr()) == lenshort);
		CHECK(s == originshort);
	}

	TEST_CASE("TestString_Swap")
	{
		String ss1("short");
		String ss2("another");
		String sl1("long string for testing purposes");
		String sl2("another long string for testing purposes");

		ss1.Swap(ss2);
		CHECK(ss1 == "another");
		CHECK(ss2 == "short");

		sl1.Swap(sl2);
		CHECK(sl1 == "another long string for testing purposes");
		CHECK(sl2 == "long string for testing purposes");

		ss1.Swap(sl2);
		CHECK(ss1 == "long string for testing purposes");
		CHECK(sl2 == "another");

		sl1.Swap(ss2);
		CHECK(sl1 == "short");
		CHECK(ss2 == "another long string for testing purposes");
	}

	TEST_CASE("TestString_Equal")
	{
		String s("hello");
		CHECK(s == String("hello"));
		CHECK(s == "hello");
		CHECK("hello" == s);
		CHECK(s != String("hello world"));
		CHECK(s != "hello world");
		CHECK("hello world" != s);
	}

	TEST_CASE("TestString_ltgt")
	{
		String s("hello");
		CHECK(!(s < "hello"));
		CHECK(s < "helloo");
		CHECK(s < "hello0");
		CHECK(s > "he1");
		CHECK(s > "hell");
		CHECK(s > "a");
		CHECK(s < "z");
		CHECK(s > "aaaaaaaa");
		CHECK(s < "zzzzzzzz");
		CHECK(s > "hella");
		CHECK(s < "hellz");
		CHECK(s < "hellz");
	}

	TEST_CASE("TestString_lege")
	{
		String s("hello");
		CHECK(s <= "hello");
		CHECK(s >= "hello");
		CHECK(s <= "helloo");
		CHECK(s <= "hello0");
		CHECK(s >= "he1");
		CHECK(s >= "hell");
		CHECK(s >= "a");
		CHECK(s <= "z");
		CHECK(s >= "aaaaaaaa");
		CHECK(s <= "zzzzzzzz");
		CHECK(s >= "hella");
		CHECK(s <= "hellz");
		CHECK(s <= "hellz");
	}

	TEST_CASE("TestString_Append")
	{
		String s;
		s += "hello";
		s += ' ';
		s += "world";
		CHECK(s == "hello world");
		s += " and this is a very long string";
		CHECK(s == "hello world and this is a very long string");
	}

	TEST_CASE("TestString_add")
	{
		CHECK(String("hello") + String(" world") == "hello world");
		CHECK(String("hello") + " world" == "hello world");
		CHECK(String("hello") + " " + "world" == "hello world");
		CHECK("hello" + String(" ") + "world" == "hello world");
	}

	TEST_CASE("TestString_Insert")
	{
		String s("world");
		s.Insert(s.end(), '!');
		CHECK(s == "world!");
		s.Insert(s.begin(), "hello");
		CHECK(s == "helloworld!");
		s.Insert(s.begin() + 5, " ");
		CHECK(s == "hello world!");
		s.Insert(s.end() - 1, ", prepend a huge string to check");
		CHECK(s == "hello world, prepend a huge string to check!");
	}

	TEST_CASE("TestString_Erase")
	{
		String s("hello");
		s.Erase(s.begin(), s.end());
		CHECK(s.Empty());
		s = "hello";
		s.Erase(s.end() - 1, s.end());
		CHECK(s == "hell");
		s = "hello world and this is a very long string";
		s.Erase(s.begin(), s.begin() + 4);
		CHECK(s == "o world and this is a very long string");
		s.Erase(s.begin(), s.end());
		CHECK(s.Empty());
	}

	TEST_CASE("TestString_Reserve")
	{
		String s;
		s.Reserve(0);
		CHECK(s.Capacity() == 17);
		s.Reserve(10);
		s = "short";
		CHECK(s.Capacity() == 17);
		CHECK(s == "short");
		s.Reserve(17);
		CHECK(s.Capacity() == 17);
		CHECK(s == "short");
		s.Reserve(100);
		CHECK(s.Capacity() == 100);
		CHECK(s == "short");
		s.Reserve(101);
		CHECK(s.Capacity() == 101);
		CHECK(s == "short");
	}

	TEST_CASE("TestString_Resize")
	{
		String s;
		s.Resize(1, ' ');
		CHECK(s == " ");
		s.Resize(16, '+');
		CHECK(s == " +++++++++++++++");
		s.Clear();
		s.Resize(16, '@');
		CHECK(s == "@@@@@@@@@@@@@@@@");
		s.Resize(12, '-');
		CHECK(s == "@@@@@@@@@@@@");
	}

	//    TEST_CASE("TestString_Hash")
	//    {
	//        String s{"AAAAAAAA"};
	//        auto hash = hashValue(s);
	//        CHECK(hash != 0);
	//        CHECK(hash != USIZE_MAX);
	//    }

	TEST_CASE("TestString_Append_Types")
	{
		{
			String s{};
			s.Append('a');
			s.Append('b');
			CHECK(!s.Empty());
			CHECK(strstr(s.CStr(), "ab") != nullptr);
		}
	}

	TEST_CASE("Core::StringBasics")
	{
		String str = "abcdef";
		CHECK(str.Find('c') == 2);
		CHECK(str.Find('d') != 2);
		CHECK(str.Find('x') == nPos);
	}

	TEST_CASE("Core::StringViewBasis")
	{
		StringView stringView = {"abcdce"};
		CHECK(!stringView.Empty());

		CHECK(stringView.FindFirstOf("c") == 2);
		CHECK(stringView.FindFirstOf('c') == 2);
		CHECK(stringView.FindFirstNotOf("a") == 1);
		CHECK(stringView.FindLastOf("c") == 4);
		CHECK(stringView.FindLastNotOf("e") == 4);


		CHECK(stringView.StartsWith("ab"));
		CHECK(!stringView.StartsWith("zxc"));
	}

	TEST_CASE("Core::HashSetBasics")
	{
		HashSet<i32> set{};

		for (int i = 0; i < 1000; ++i)
		{
			set.Insert(i);
		}

		bool check = true;

		CHECK(set.Has(100));

		for (int i = 0; i < 1000; ++i)
		{
			HashSet<i32>::Iterator it = set.Find(i);
			if (it == set.end())
			{
				REQUIRE(false);
			}
		}

		CHECK(check);

		set.Clear();

		HashSet<i32>::Iterator it = set.Find(1);
		REQUIRE(it == set.end());
	}

	TEST_CASE("Core::HashSetForeach")
	{
		HashSet<i32> set{};
		set.Insert(20);
		set.Insert(40);

		const HashSet<i32> set2 = set;

		i32 sum = 0;
		for (const i32& vl : set2)
		{
			sum += vl;
		}
		CHECK(sum == 60);
	}

	TEST_CASE("Core::HashSetTestMove")
	{
		HashSet<i32> set{};
		set.Insert(20);
		set.Insert(40);

		HashSet<i32> other{Traits::Move(set)};
		CHECK(other.Has(40));
		CHECK(set.Empty());
	}

	TEST_CASE("Core::HashSetTestCopy")
	{
		HashSet<i32> set{};
		set.Insert(20);
		set.Insert(40);

		HashSet<i32> other = set;

		CHECK(set.Has(20));
		CHECK(set.Has(40));

		CHECK(other.Has(20));
		CHECK(other.Has(40));

		CHECK(other.Size() == 2);
	}

	TEST_CASE("Core::HashMapTestErase")
	{
		HashSet<i32> set{};
		set.Insert(20);
		set.Insert(40);

		set.Erase(20);

		CHECK(set.Find(20) == set.end());
		CHECK(set.Find(40) != set.end());

		set.Erase(40);

		CHECK(set.Find(2) == set.end());
	}

	TEST_CASE("Core::HashMapTestStr")
	{
		HashSet<String> set{};
		set.Insert("AAAA");
		set.Insert("CCCC");

		for (int i = 0; i < 10000; ++i)
		{
			std::string str = std::to_string(i);
			set.Insert(String{str.c_str()});
		}

		{
			auto it = set.Find("CCCC");
			REQUIRE(it != set.end());
		}

		{
			StringView strView = {"AAAA"};
			auto       it = set.Find(strView);
			REQUIRE(it != set.end());
		}
	}

	using MyCustomEvent = EventType<"Event::MyCustomEvent"_h, void(i32 a, i32 b)>;

	i32 sumRes = 0;

	void Sum(i32 a, i32 b)
	{
		sumRes = a + b;
	}

	TEST_CASE("Core::EventsGlobalFunc")
	{
		Event::Reset();

		Event::Bind<MyCustomEvent, &Sum>();
		CHECK(Event::EventCount<MyCustomEvent>() == 1);

		EventHandler<MyCustomEvent> eventHandler{};
		eventHandler.Invoke(10, 20);
		CHECK(sumRes == 30);

		Event::Unbind<MyCustomEvent, &Sum>();
		CHECK(Event::EventCount<MyCustomEvent>() == 0);
	}


	struct TestEventClass
	{
		i32 sumRes = 0;
		i32 otherValue = 0;

		static i32 sumResStatic;

		void Sum(i32 a, i32 b)
		{
			sumRes = a + b;
		}

		void SumConst(i32 a, i32 b) const
		{
			sumResStatic = otherValue + a + b;
		}
	};

	i32 TestEventClass::sumResStatic = 0;

	TEST_CASE("Core::EventsClassFunc")
	{
		Event::Reset();

		TestEventClass eventClass{
			.otherValue = 10
		};

		Event::Bind<MyCustomEvent, &TestEventClass::Sum>(&eventClass);
		Event::Bind<MyCustomEvent, &TestEventClass::SumConst>(&eventClass);

		CHECK(Event::EventCount<MyCustomEvent>() == 2);

		EventHandler<MyCustomEvent> eventHandler{};
		eventHandler.Invoke(40, 50);
		CHECK(eventClass.sumRes == 90);
		CHECK(eventClass.sumResStatic == 100);

		Event::Unbind<MyCustomEvent, &TestEventClass::Sum>(&eventClass);
		Event::Unbind<MyCustomEvent, &TestEventClass::SumConst>(&eventClass);

		CHECK(Event::EventCount<MyCustomEvent>() == 0);
	}

	TEST_CASE("Core::StringView::Comparator")
	{
		SUBCASE("Equal strings")
		{
			StringView s1("hello");
			StringView s2("hello");
			CHECK(s1.Compare(s2) == 0);
		}

		SUBCASE("Different strings - first character differs")
		{
			StringView s1("apple");
			StringView s2("banana");
			CHECK(s1.Compare(s2) > 0); // 'b' > 'a' in the compared string (s2)
			CHECK(s2.Compare(s1) < 0); // 'a' < 'b' in the compared string (s1)
		}

		SUBCASE("Different strings - later character differs")
		{
			StringView s1("test");
			StringView s2("team");
			CHECK(s1.Compare(s2) < 0); // 'a' < 's' in the compared string (s2)
			CHECK(s2.Compare(s1) > 0); // 's' > 'a' in the compared string (s1)
		}

		SUBCASE("Prefix string")
		{
			StringView s1("test");
			StringView s2("testing");
			CHECK(s1.Compare(s2) > 0); // compared string (s2) is longer
			CHECK(s2.Compare(s1) < 0); // compared string (s1) is shorter
		}

		SUBCASE("Empty string")
		{
			StringView s1("");
			StringView s2("test");
			CHECK(s1.Compare(s2) > 0); // compared string (s2) is longer
			CHECK(s2.Compare(s1) < 0); // compared string (s1) is shorter

			StringView s3("");
			CHECK(s1.Compare(s3) == 0); // empty == empty
		}

		SUBCASE("Case sensitivity")
		{
			StringView s1("Test");
			StringView s2("test");
			CHECK(s1.Compare(s2) > 0); // 't' > 'T' in the compared string (s2)
			CHECK(s2.Compare(s1) < 0); // 'T' < 't' in the compared string (s1)
		}

		SUBCASE("Special characters")
		{
			StringView s1("test!");
			StringView s2("test?");
			CHECK(s1.Compare(s2) > 0); // '?' > '!' in the compared string (s2)
			CHECK(s2.Compare(s1) < 0); // '!' < '?' in the compared string (s1)
		}

		SUBCASE("Unicode characters")
		{
			StringView s1("café");
			StringView s2("cafe");
			// Note: This assumes Type is char and we're using a single-byte encoding
			// Real Unicode support would need a different implementation
			CHECK(s1.Compare(s2) != 0); // Different strings
		}

		SUBCASE("Numeric characters")
		{
			StringView s1("test1");
			StringView s2("test2");
			CHECK(s1.Compare(s2) > 0); // '2' > '1' in the compared string (s2)
		}
	}

	TEST_CASE("Core::UUID")
	{
		UUID uuid = UUID::FromString("9e7583c9-e6dd-4c96-a59e-c5a6c8938f72");
		CHECK(uuid.firstValue);
		CHECK(uuid.secondValue);

		String uuidStr = uuid.ToString();

		CHECK(uuid.ToString() == "9e7583c9-e6dd-4c96-a59e-c5a6c8938f72");
	}


	// Basic functionality tests
	TEST_CASE("Queue construction and basic state")
	{
		SUBCASE("Default constructor")
		{
			Queue<int> q;
			CHECK(q.IsEmpty());
			CHECK_EQ(q.GetSize(), 0);
			CHECK_EQ(q.GetCapacity(), 10);
			CHECK_FALSE(q.IsFull());
		}

		SUBCASE("Constructor with custom capacity")
		{
			Queue<double> q(5);
			CHECK(q.IsEmpty());
			CHECK_EQ(q.GetSize(), 0);
			CHECK_EQ(q.GetCapacity(), 5);
		}
	}

	// Enqueue operation tests
	TEST_CASE("Queue Enqueue operation")
	{
		Queue<int> q(3);

		SUBCASE("Single element enqueue")
		{
			q.Enqueue(42);
			CHECK_EQ(q.GetSize(), 1);
			CHECK_EQ(q.Peek(), 42);
			CHECK_FALSE(q.IsEmpty());
			CHECK_FALSE(q.IsFull());
		}

		SUBCASE("Fill queue to capacity")
		{
			q.Enqueue(1);
			q.Enqueue(2);
			q.Enqueue(3);
			CHECK_EQ(q.GetSize(), 3);
			CHECK(q.IsFull());
			CHECK_EQ(q.Peek(), 1);
		}

		SUBCASE("Enqueue that triggers resize")
		{
			q.Enqueue(1);
			q.Enqueue(2);
			q.Enqueue(3);
			// This should trigger resize
			q.Enqueue(4);
			CHECK_EQ(q.GetSize(), 4);
			CHECK_EQ(q.GetCapacity(), 6); // Doubled from 3
			CHECK_FALSE(q.IsFull());

			// Verify all elements are still there in correct order
			CHECK_EQ(q.Dequeue(), 1);
			CHECK_EQ(q.Dequeue(), 2);
			CHECK_EQ(q.Dequeue(), 3);
			CHECK_EQ(q.Dequeue(), 4);
		}
	}

	// Dequeue operation tests
	TEST_CASE("Queue Dequeue operation")
	{
		Queue<int> q;

		SUBCASE("Dequeue after enqueue")
		{
			q.Enqueue(10);
			q.Enqueue(20);
			CHECK_EQ(q.Dequeue(), 10);
			CHECK_EQ(q.GetSize(), 1);
			CHECK_EQ(q.Peek(), 20);
		}

		SUBCASE("Dequeue until empty")
		{
			q.Enqueue(1);
			q.Enqueue(2);
			q.Enqueue(3);

			CHECK_EQ(q.Dequeue(), 1);
			CHECK_EQ(q.Dequeue(), 2);
			CHECK_EQ(q.Dequeue(), 3);
			CHECK(q.IsEmpty());
		}
	}

	// Peek operation tests
	TEST_CASE("Queue Peek operation")
	{
		Queue<int> q;

		SUBCASE("Peek after enqueue")
		{
			q.Enqueue(42);
			CHECK_EQ(q.Peek(), 42);
			// Make sure peek doesn't remove the element
			CHECK_EQ(q.GetSize(), 1);
			CHECK_EQ(q.Peek(), 42);
		}

		SUBCASE("Peek after multiple operations")
		{
			q.Enqueue(10);
			q.Enqueue(20);
			CHECK_EQ(q.Peek(), 10);
			q.Dequeue();
			CHECK_EQ(q.Peek(), 20);
		}
	}

	// Resize behavior tests
	TEST_CASE("Queue resize behavior")
	{
		Queue<int> q(4);

		SUBCASE("Grow when full")
		{
			// Fill the queue
			for (int i = 0; i < 4; i++)
			{
				q.Enqueue(i);
			}
			CHECK_EQ(q.GetCapacity(), 4);

			// Trigger growth
			q.Enqueue(100);
			CHECK_EQ(q.GetCapacity(), 8); // Should double
			CHECK_EQ(q.GetSize(), 5);

			// Check all elements are preserved
			for (int i = 0; i < 4; i++)
			{
				CHECK_EQ(q.Dequeue(), i);
			}
			CHECK_EQ(q.Dequeue(), 100);
		}

		SUBCASE("Shrink when mostly empty")
		{
			// Fill the queue with many elements to trigger growth
			for (int i = 0; i < 20; i++)
			{
				q.Enqueue(i);
			}
			int capacity = q.GetCapacity();
			CHECK_GT(capacity, 16); // Should be at least this big

			// Dequeue most elements to trigger shrink
			for (int i = 0; i < 15; i++)
			{
				q.Dequeue();
			}

			// After shrinking, capacity should be reduced
			CHECK_LT(q.GetCapacity(), capacity);
			CHECK_EQ(q.GetSize(), 5);

			// Check remaining elements are preserved
			for (int i = 15; i < 20; i++)
			{
				CHECK_EQ(q.Dequeue(), i);
			}
		}
	}

	// Circular buffer behavior tests
	TEST_CASE("Queue circular buffer behavior")
	{
		Queue<int> q(5);

		// Fill the queue
		for (int i = 0; i < 5; i++)
		{
			q.Enqueue(i);
		}

		// Remove a few elements
		q.Dequeue();
		q.Dequeue();

		// Add a few more (should wrap around in the circular buffer)
		q.Enqueue(5);
		q.Enqueue(6);

		// Check elements come out in correct order
		for (int i = 2; i <= 6; i++)
		{
			CHECK_EQ(q.Dequeue(), i);
		}
	}

	// Test queue with different data types
	TEST_CASE("Queue with different data types")
	{
		SUBCASE("Queue of strings")
		{
			Queue<std::string> q;
			q.Enqueue("Hello");
			q.Enqueue("World");
			CHECK_EQ(q.Dequeue(), "Hello");
			CHECK_EQ(q.Peek(), "World");
		}

		SUBCASE("Queue of doubles")
		{
			Queue<double> q;
			q.Enqueue(3.14);
			q.Enqueue(2.71);
			CHECK_EQ(q.Dequeue(), 3.14);
			CHECK_EQ(q.Peek(), 2.71);
		}
	}

	class DestructionTracker
	{
	public:
		DestructionTracker(bool* isDestroyed)
			: wasDestroyed(isDestroyed)
		{
			*wasDestroyed = false;
		}

		~DestructionTracker()
		{
			*wasDestroyed = true;
		}

	private:
		bool* wasDestroyed;
	};

	TEST_CASE("Ref basic functionality")
	{
		SUBCASE("Default constructor creates null reference")
		{
			Ref<int> ref;
			CHECK(ref.Get() == nullptr);
			CHECK(ref.UseCount() == 0);
			CHECK_FALSE(static_cast<bool>(ref));
		}

		SUBCASE("Constructor with raw pointer")
		{
			Allocator* allocator = MemoryGlobals::GetDefaultAllocator();
			int* value = allocator->Alloc<int>();
			*value = 42;
			Ref<int> ref(value, allocator);
			CHECK(ref.Get() != nullptr);
			CHECK(ref.UseCount() == 1);
			CHECK(*ref == 42);
			CHECK(static_cast<bool>(ref));
		}

		SUBCASE("Make static factory method")
		{
			Ref<std::string> ref = MakeRef<std::string>("test");
			CHECK(ref.UseCount() == 1);
			CHECK(*ref == "test");
		}
	}

	TEST_CASE("Ref destructor and cleanup")
	{
		SUBCASE("Object is destroyed when reference count reaches zero")
		{
			bool isDestroyed = false;
			{
				Ref<DestructionTracker> ref = MakeRef<DestructionTracker>(&isDestroyed);
				CHECK_FALSE(isDestroyed);
			}
			CHECK(isDestroyed);
		}
	}

	TEST_CASE("Ref copy semantics")
	{
		SUBCASE("Copy constructor increases reference count")
		{
			Ref<int> ref1 = MakeRef<int>(42);
			Ref<int> ref2 = ref1;

			CHECK(ref1.UseCount() == 2);
			CHECK(ref2.UseCount() == 2);
			CHECK(ref1.Get() == ref2.Get());
		}

		SUBCASE("Copy assignment increases reference count")
		{
			Ref<int> ref1 = MakeRef<int>(42);
			Ref<int> ref2;
			ref2 = ref1;

			CHECK(ref1.UseCount() == 2);
			CHECK(ref2.UseCount() == 2);
			CHECK(ref1.Get() == ref2.Get());
		}

		SUBCASE("Copying null reference")
		{
			Ref<int> ref1;
			Ref<int> ref2 = ref1;

			CHECK(ref1.UseCount() == 0);
			CHECK(ref2.UseCount() == 0);
			CHECK(ref1.Get() == nullptr);
			CHECK(ref2.Get() == nullptr);
		}
	}

	TEST_CASE("Ref move semantics")
	{
		SUBCASE("Move constructor transfers ownership")
		{
			Ref<int> ref1 = MakeRef<int>(42);
			Ref<int> ref2 = std::move(ref1);

			CHECK(ref1.Get() == nullptr);
			CHECK(ref1.UseCount() == 0);
			CHECK(ref2.UseCount() == 1);
			CHECK(*ref2 == 42);
		}

		SUBCASE("Move assignment transfers ownership")
		{
			Ref<int> ref1 = MakeRef<int>(42);
			Ref<int> ref2;
			ref2 = std::move(ref1);

			CHECK(ref1.Get() == nullptr);
			CHECK(ref1.UseCount() == 0);
			CHECK(ref2.UseCount() == 1);
			CHECK(*ref2 == 42);
		}
	}

	TEST_CASE("Ref reset and swap")
	{
		SUBCASE("Reset with no arguments")
		{
			Ref<int> ref = MakeRef<int>(42);
			ref.Reset();

			CHECK(ref.Get() == nullptr);
			CHECK(ref.UseCount() == 0);
		}

		SUBCASE("Reset with new pointer")
		{
			Ref<int> ref = MakeRef<int>(42);
			int* newValue = MemoryGlobals::GetDefaultAllocator()->Alloc<int>();
			*newValue = 100;
			ref.Reset(newValue);

			CHECK(ref.UseCount() == 1);
			CHECK(*ref == 100);
		}

		SUBCASE("Swap method")
		{
			Ref<int> ref1 = MakeRef<int>(42);
			Ref<int> ref2 = MakeRef<int>(100);

			ref1.Swap(ref2);

			CHECK(*ref1 == 100);
			CHECK(*ref2 == 42);
			CHECK(ref1.UseCount() == 1);
			CHECK(ref2.UseCount() == 1);
		}

		SUBCASE("Non-member swap function")
		{
			Ref<int> ref1 = MakeRef<int>(42);
			Ref<int> ref2 = MakeRef<int>(100);

			Swap(ref1, ref2);

			CHECK(*ref1 == 100);
			CHECK(*ref2 == 42);
		}
	}

	class Base
	{
	public:
		virtual ~Base() = default;

		virtual int GetValue() const
		{
			return 1;
		}
	};

	class Derived : public Base
	{
	public:
		int GetValue() const override
		{
			return 2;
		}

		void DerivedOnly() {}
	};

	TEST_CASE("Ref pointer casts")
	{
		SUBCASE("StaticPointerCast")
		{
			Ref<Derived> derivedRef = MakeRef<Derived>();
			Ref<Base>    baseRef = StaticPointerCast<Base>(derivedRef);

			CHECK(derivedRef.UseCount() == 2);
			CHECK(baseRef.UseCount() == 2);
			CHECK(baseRef->GetValue() == 2);
		}

		SUBCASE("DynamicPointerCast - successful cast")
		{
			Ref<Base>    baseRef = MakeRef<Derived>();
			Ref<Derived> derivedRef = DynamicPointerCast<Derived>(baseRef);

			CHECK(derivedRef.Get() != nullptr);
			CHECK(baseRef.UseCount() == 2);
			CHECK(derivedRef.UseCount() == 2);
			CHECK(derivedRef->GetValue() == 2);
		}

		SUBCASE("DynamicPointerCast - failed cast")
		{
			Ref<Base>    baseRef = MakeRef<Base>();
			Ref<Derived> derivedRef = DynamicPointerCast<Derived>(baseRef);

			CHECK(derivedRef.Get() == nullptr);
			CHECK(baseRef.UseCount() == 1);
			CHECK(derivedRef.UseCount() == 0);
		}

		SUBCASE("ConstPointerCast")
		{
			Ref<const int> constRef = MakeRef<int>(42);
			Ref<int>       mutableRef = ConstPointerCast<int>(constRef);

			CHECK(constRef.UseCount() == 2);
			CHECK(mutableRef.UseCount() == 2);
			*mutableRef = 100;
			CHECK(*constRef == 100);
		}
	}

	TEST_CASE("Ref thread safety")
	{
		SUBCASE("Multiple threads accessing the same reference")
		{
			auto ref = MakeRef<std::atomic<int>>(0);
			const int numThreads = 10;
			const int iterations = 1000;

			std::vector<std::thread> threads;
			for (int i = 0; i < numThreads; ++i)
			{
				threads.emplace_back([ref, iterations]() {
					for (int j = 0; j < iterations; ++j)
					{
						Ref<std::atomic<int>> localRef = ref;
						localRef->fetch_add(1, std::memory_order_relaxed);
					}
				});
			}

			for (auto& thread : threads)
			{
				thread.join();
			}

			CHECK(ref->load() == numThreads * iterations);
		}

		SUBCASE("Multiple threads creating and destroying references")
		{
			auto      ref = MakeRef<int>(42);
			const int numThreads = 10;
			const int iterations = 1000;

			std::vector<std::thread> threads;
			for (int i = 0; i < numThreads; ++i)
			{
				threads.emplace_back([ref, iterations]()
				{
					for (int j = 0; j < iterations; ++j)
					{
						Ref<int> localRef1 = ref;
						Ref<int> localRef2 = localRef1;
						Ref<int> localRef3;
						localRef3 = localRef2;
					}
				});
			}

			for (auto& thread : threads)
			{
				thread.join();
			}

			CHECK(ref.UseCount() == 1);
			CHECK(*ref == 42);
		}
	}

	TEST_CASE("Core::Variant")
	{
		{
			Variant a = 10;
			CHECK(a.GetType() == Variant::Type::Int);
			CHECK(static_cast<i32>(a) == 10);
		}

		{
			Variant a = 10.0f;
			CHECK(a.GetType() == Variant::Type::Float);
			CHECK(static_cast<f32>(a) == 10.0f);
		}

		{
			Variant a = "Hello";
			CHECK(a.GetType() == Variant::Type::String);
			CHECK(a == "Hello");
		}

		{
			Variant a = 10;
			Variant b = 10;
			CHECK(a == b);
		}

		{
			HashSet<Variant> set;
			set.Insert(10);
			set.Insert(20);
			set.Insert("Hello");


			CHECK(set.Has(Variant(10)));
			CHECK(set.Has(Variant("Hello")));

		}

	}
}
