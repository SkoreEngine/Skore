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

#include "FileSystem.hpp"
#include "Path.hpp"
#include "Skore/Core/UUID.hpp"

#if defined(SK_WIN)

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <direct.h>
#include <KnownFolders.h>
#include <ShlObj.h>
#include <cstdio>

#ifdef CreateDirectory
#undef CreateDirectory
#endif

#ifdef CopyFile
#undef CopyFile
#endif

#ifdef CreateFileMapping
#undef CreateFileMapping
#endif

namespace Skore
{
    DirIterator& DirIterator::operator++()
    {
        if (m_handler)
        {
            WIN32_FIND_DATA fd{};
            auto res = FindNextFile(m_handler, &fd);
            if (res != 0)
            {
                do
                {
                    bool isDirEntry = !(strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0);
                    if (isDirEntry)
                    {
                        m_path = Path::Join(m_directory, fd.cFileName);
                        return *this;
                    }
                } while (::FindNextFile(m_handler, &fd) != 0);
            }
            FindClose(m_handler);
            m_handler = nullptr;
        }
        m_path.Clear();
        return *this;
    }

	DirIterator::DirIterator(const StringView& directory) : m_directory(directory), m_handler(nullptr)
	{
		WIN32_FIND_DATA fd{};
		char cwd[MAX_PATH];
		sprintf(cwd, "%s\\*.*", directory.CStr());
		m_handler = FindFirstFile(cwd, &fd);

		if (m_handler != INVALID_HANDLE_VALUE)
		{
			do
			{
				bool isDirEntry = !(strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0);
				if (isDirEntry)
				{
					m_path = Path::Join(directory, fd.cFileName);
					break;
				}
			} while (::FindNextFile(m_handler, &fd) != 0);
		}
	}

	DirIterator::~DirIterator()
	{
		if (m_handler)
		{
			FindClose(m_handler);
		}
	}

	String FileSystem::CurrentDir()
	{
		TCHAR path[MAX_PATH];
		GetCurrentDirectory(MAX_PATH, path);
		return {path, strlen(path)};
	}

    String FileSystem::DocumentsDir()
	{
		CHAR myDocuments[MAX_PATH];
		HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, myDocuments);
		return {myDocuments};
	}

	FileStatus FileSystem::GetFileStatus(const StringView& path)
	{
		WIN32_FILE_ATTRIBUTE_DATA fileAttrData = {0};
		bool exists = GetFileAttributesEx(path.CStr(), GetFileExInfoStandard, &fileAttrData);

        LARGE_INTEGER size{};
        size.HighPart = fileAttrData.nFileSizeHigh;
        size.LowPart = fileAttrData.nFileSizeLow;

		FileStatus fileStatus = FileStatus{
            .exists = exists,
            .isDirectory = (fileAttrData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
            .lastModifiedTime = (u64) (static_cast<i64>(fileAttrData.ftLastWriteTime.dwHighDateTime) << 32 | fileAttrData.ftLastWriteTime.dwLowDateTime),
            .fileSize = (u64) size.QuadPart,
        };

    	if (exists)
    	{
    		HANDLE file = CreateFile(path.CStr(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, !fileStatus.isDirectory ? FILE_ATTRIBUTE_NORMAL : FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    		if (file != INVALID_HANDLE_VALUE)
    		{
    			FILE_ID_INFO fileIdInfo;
    			GetFileInformationByHandleEx(file, FileIdInfo, &fileIdInfo, sizeof(FILE_ID_INFO));
    			fileStatus.fileId = HashValue(fileIdInfo.FileId.Identifier);
    			CloseHandle(file);
    		}
    	}

    	return fileStatus;
	}

	u64 FileSystem::GetFileSize(const StringView& path)
    {
    	WIN32_FILE_ATTRIBUTE_DATA fileAttrData = {0};
    	bool exists = GetFileAttributesEx(path.CStr(), GetFileExInfoStandard, &fileAttrData);

    	LARGE_INTEGER size{};
    	size.HighPart = fileAttrData.nFileSizeHigh;
    	size.LowPart = fileAttrData.nFileSizeLow;

    	return static_cast<u64>(size.QuadPart);
    }

	String FileSystem::AppFolder()
    {
        PWSTR pathTemp;
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &pathTemp);
        char buffer[MAX_PATH];
        usize size{};
        wcstombs_s(&size, buffer, pathTemp, MAX_PATH);
        CoTaskMemFree(pathTemp);
        return {buffer, size - 1};
    }

    FileHandler FileSystem::OpenFile(const StringView& path, AccessMode accessMode)
    {
        DWORD dwShareMode = 0;
        DWORD dwDesiredAccess = 0;
    	DWORD flags = 0;
        if (accessMode == AccessMode::ReadOnly)
        {
            dwDesiredAccess = GENERIC_READ;
            dwShareMode = OPEN_EXISTING;
        	flags = FILE_FLAG_OVERLAPPED;
        }

        if (accessMode == AccessMode::WriteOnly)
        {
            dwDesiredAccess = GENERIC_WRITE;
            dwShareMode = CREATE_ALWAYS;
        }

        if (accessMode == AccessMode::ReadAndWrite)
        {
            dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
            dwShareMode = CREATE_NEW;
        }

        HANDLE hout = CreateFile(path.CStr(), dwDesiredAccess, 0, nullptr, dwShareMode, FILE_ATTRIBUTE_NORMAL, &flags);
        if (hout == INVALID_HANDLE_VALUE)
        {
            return FileHandler{};
        }
        return FileHandler{hout};
    }

    usize FileSystem::GetFileSize(FileHandler fileHandler)
    {
        LARGE_INTEGER size;
        if (!GetFileSizeEx(fileHandler.ToPtr(), &size))
        {
            return 0;
        }
        return size.QuadPart;
    }

    u64 FileSystem::WriteFile(FileHandler fileHandler, ConstPtr data, usize size)
    {
        DWORD nWritten;
        ::WriteFile(fileHandler.ToPtr(), data, size, &nWritten, nullptr);
        return nWritten;
    }

    u64 FileSystem::ReadFile(FileHandler fileHandler, VoidPtr data, usize size)
    {
    	OVERLAPPED overlapped = {};

        DWORD nRead;
        ::ReadFile(fileHandler.ToPtr(), data, size, &nRead, &overlapped);
        return nRead;
    }

	u64 FileSystem::ReadFileAt(FileHandler fileHandler, VoidPtr data, usize size, usize offset)
    {
    	OVERLAPPED overlapped{};

    	union {
    		u64 value{};
    		struct {
    			u32 low;
    			u32 hi;
    		};
    	};
    	value = offset;

    	overlapped.Offset = low;
    	overlapped.OffsetHigh = hi;

    	DWORD nRead;
    	::ReadFile(fileHandler.ToPtr(), data, size, &nRead, &overlapped);
    	return nRead;
    }

	FileHandler FileSystem::CreateFileMapping(FileHandler fileHandler, AccessMode accessMode, usize size)
    {
		DWORD protect = 0;
    	switch (accessMode)
	    {
		    case AccessMode::ReadOnly:
			    protect = PAGE_READONLY;
    			break;
		    case AccessMode::WriteOnly:
		    case AccessMode::ReadAndWrite:
		    	protect = PAGE_READWRITE;
			    break;
	    }

    	HANDLE hout = ::CreateFileMappingA(fileHandler.ToPtr(), nullptr, protect, 0, size, nullptr);

		if (hout == INVALID_HANDLE_VALUE)
    	{
    		return FileHandler{};
    	}
    	return FileHandler{hout};
    }

	VoidPtr FileSystem::MapViewOfFile(FileHandler fileHandler)
    {
    	return ::MapViewOfFile(fileHandler.ToPtr(), FILE_MAP_ALL_ACCESS, 0, 0, 0);
    }

	bool FileSystem::UnmapViewOfFile(VoidPtr map)
    {
	    return ::UnmapViewOfFile(map);
    }

	void FileSystem::CloseFileMapping(FileHandler fileHandler)
    {
    	CloseHandle(fileHandler.ToPtr());
    }

    void FileSystem::CloseFile(FileHandler fileHandler)
    {
        CloseHandle(fileHandler.ToPtr());
    }
}

#endif