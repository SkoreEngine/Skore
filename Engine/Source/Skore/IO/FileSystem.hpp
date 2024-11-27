#pragma once

#include "FileTypes.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/Array.hpp"

namespace Skore::FileSystem
{
    SK_API String CurrentDir();
    SK_API String DocumentsDir();
    SK_API String AppFolder();
    SK_API String AssetFolder();
    SK_API String TempFolder();

    SK_API FileStatus GetFileStatus(const StringView& path);
    SK_API bool       CreateDirectory(const StringView& path);
    SK_API bool       Remove(const StringView& path);
    SK_API bool       Rename(const StringView& oldName, const StringView& newName);
    SK_API bool       CopyFile(const StringView& from, const StringView& to);

    SK_API FileHandler OpenFile(const StringView& path, AccessMode accessMode);
    SK_API u64         GetFileSize(FileHandler fileHandler);
    SK_API u64         WriteFile(FileHandler fileHandler, ConstPtr data, usize size);
    SK_API u64         ReadFile(FileHandler fileHandler, VoidPtr data, usize size);
    SK_API u64         ReadFileAt(FileHandler fileHandler, VoidPtr data, usize size, usize offset);
    SK_API void        CloseFile(FileHandler fileHandler);
    SK_API FileHandler CreateFileMapping(FileHandler fileHandler, AccessMode accessMode, usize size);
    SK_API VoidPtr     MapViewOfFile(FileHandler fileHandler);
    SK_API bool        UnmapViewOfFile(VoidPtr map);
    SK_API void        CloseFileMapping(FileHandler fileHandler);

    SK_API String    ReadFileAsString(const StringView& path);
    SK_API Array<u8> ReadFileAsByteArray(const StringView& path);
    SK_API void      SaveFileAsString(const StringView& path, const StringView& string);
}
