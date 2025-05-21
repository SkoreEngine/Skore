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


#include <doctest.h>
#include "Skore/IO/Path.hpp"

using namespace Skore;

namespace
{
	TEST_CASE("IO::PathBasics")
	{
		String check{};
		check += "C:";
		check += SK_PATH_SEPARATOR;
		check += "Folder1";
		check += SK_PATH_SEPARATOR;
		check += "Folder2";
		check += SK_PATH_SEPARATOR;
		check += "Folder3";
		check += SK_PATH_SEPARATOR;
		check += "Folder4";

		String parent = Path::Join("C:/", "Folder1/", "/Folder2", "Folder3/Folder4");
		CHECK(!parent.Empty());
		CHECK(check == parent);

		String file = Path::Join(parent, "Leaf.exe");

		check += SK_PATH_SEPARATOR;
		check += "Leaf.exe";
		CHECK(file == check);

		CHECK(!file.Empty());
		CHECK(Path::Extension(file) == ".exe");
		CHECK(Path::Name(file) == "Leaf");
		CHECK(Path::Parent(file) == parent);
	}
}