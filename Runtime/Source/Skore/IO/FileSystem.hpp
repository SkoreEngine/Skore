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

#pragma once

#include "FileTypes.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Array.hpp"

namespace Skore
{
	//TODO - remove
	struct SK_API FileSystem
	{
		static void SetupTempFolder(StringView tempFolder);

		static String CurrentDir();
		static String DocumentsDir();
		static String AppFolder();
		static String AssetFolder();
		static String TempFolder();

		static FileStatus GetFileStatus(const StringView& path);
		static u64        GetFileSize(const StringView& path);
		static bool       CreateDirectory(const StringView& path);
		static bool       Remove(const StringView& path);
		static bool       Rename(const StringView& oldName, const StringView& newName);
		static bool       CopyFile(const StringView& from, const StringView& to);

		static FileHandler OpenFile(const StringView& path, AccessMode accessMode);
		static u64         GetFileSize(FileHandler fileHandler);
		static u64         WriteFile(FileHandler fileHandler, ConstPtr data, usize size);
		static u64         ReadFile(FileHandler fileHandler, VoidPtr data, usize size);
		static u64         ReadFileAt(FileHandler fileHandler, VoidPtr data, usize size, usize offset);
		static void        CloseFile(FileHandler fileHandler);
		static FileHandler CreateFileMapping(FileHandler fileHandler, AccessMode accessMode, usize size);
		static VoidPtr     MapViewOfFile(FileHandler fileHandler);
		static bool        UnmapViewOfFile(VoidPtr map);
		static void        CloseFileMapping(FileHandler fileHandler);

		static String    ReadFileAsString(const StringView& path);
		static void      SaveFileAsString(const StringView& path, const StringView& string);

		static Array<u8> ReadFileAsByteArray(const StringView& path);
		static void      SaveFileAsByteArray(const StringView& path, Span<u8> bytes);


		static void Reset();
	};


	struct SK_API FileSystem2
	{

	};
}