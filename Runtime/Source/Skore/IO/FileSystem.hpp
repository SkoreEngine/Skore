#pragma once

#include "FileTypes.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/ByteBuffer.hpp"

namespace Skore
{
	//TODO - remove
	struct SK_API FileSystem
	{
		static void SetupTempFolder(StringView tempFolder);

		static String CurrentDir();
		static String DocumentsDir();
		static String AppFolder();
		static String TempFolder();

		static FileStatus GetFileStatus(const StringView& path);
		static u64        GetFileSize(const StringView& path);
		static u64        GetFileId(const StringView& path);
		static bool       CreateDirectory(const StringView& path);
		static bool       Remove(const StringView& path, bool printException = true);
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

		static String ReadFileAsString(const StringView& path);
		static void   SaveFileAsString(const StringView& path, const StringView& string);

		static void ReadFileAsByteArray(const StringView& path, Array<u8>& buffer);
		static void ReadFileAsByteBuffer(const StringView& path, ByteBuffer& buffer);
		static void SaveFileAsByteArray(const StringView& path, Span<u8> bytes);


		static void Reset();
	};
}
