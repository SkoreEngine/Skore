
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

#include "Skore/Common.hpp"

#if defined(SK_UNIX)

#include <dirent.h>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#include <fcntl.h>
#include <limits.h>

#include "FileSystem.hpp"
#include "Path.hpp"

namespace Skore
{

    namespace
    {
        struct LinuxFileHandler
        {
            i32 handler{};
            String path{};
        };
    }

    DirIterator& DirIterator::operator++()
    {
        if (m_handler)
        {
            auto dir = (DIR*) m_handler;
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr)
            {
                bool isDirEntry = !(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0);
                if (isDirEntry)
                {
                    m_path = Path::Join(m_directory, entry->d_name);
                    m_handler = dir;
                    return *this;
                }
            }
            closedir((DIR*)m_handler);
            m_handler = nullptr;
        }
        m_path.Clear();
        return *this;
    }

    DirIterator::DirIterator(const StringView& directory) : m_directory(directory), m_handler(nullptr)
    {
        auto dir = opendir(directory.CStr());
        if (dir)
        {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr)
            {
                bool isDirEntry = !(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0);
                if (isDirEntry)
                {
                    m_path = Path::Join(directory, entry->d_name);
                    m_handler = dir;
                    break;
                }
            }
        }
    }

    DirIterator::~DirIterator()
    {
        if (m_handler)
        {
            closedir((DIR*)m_handler);
        }
    }

    String FileSystem::CurrentDir()
    {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr)
        {
            return {cwd, strlen(cwd)};
        }
        return {};
    }

    String FileSystem::DocumentsDir()
    {
        return AppFolder();
    }

    FileStatus FileSystem::GetFileStatus(const StringView& path)
    {
        struct stat st{};
        bool exists = stat(path.CStr(), &st) == 0;
        return {
            .exists = exists,
            .isDirectory = S_ISDIR(st.st_mode),
            .lastModifiedTime = static_cast<u64>(st.st_mtime),
            .fileSize = static_cast<u64>(st.st_size),
            .fileId = HashValue(st.st_ino)
        };
    }

    String FileSystem::AppFolder()
    {
        struct passwd *pw = getpwuid(getuid());
        const char *homedir = pw->pw_dir;
        return {homedir, strlen(homedir)};
    }

    FileHandler FileSystem::OpenFile(const StringView& path, AccessMode accessMode)
    {
        i32 flags = 0;
        i32 permission = 0;
        switch (accessMode)
        {
            case AccessMode::None:
                break;
            case AccessMode::ReadOnly:
                flags = O_RDONLY;
                break;
            case AccessMode::WriteOnly:
            {
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                permission = S_IWRITE | S_IREAD;
                break;
            }
            case AccessMode::ReadAndWrite:
            {
                flags = O_RDWR | O_CREAT | O_TRUNC;
                permission = S_IWRITE | S_IREAD;
                break;
            }
        }

        i32 ptr = open(path.CStr(), flags, permission);
        if (ptr == -1)
        {
            return {};
        }
        return {MemoryGlobals::GetDefaultAllocator().Alloc<LinuxFileHandler>(ptr, path)};
    }

    u64 FileSystem::GetFileSize(FileHandler fileHandler)
    {
        LinuxFileHandler* linuxFileHandler = static_cast<LinuxFileHandler*>(fileHandler.handler);
        struct stat st{};
        stat(linuxFileHandler->path.CStr(), &st);
        return st.st_size;
    }

    u64 FileSystem::WriteFile(FileHandler fileHandler, ConstPtr data, usize size)
    {
        LinuxFileHandler* linuxFileHandler = static_cast<LinuxFileHandler*>(fileHandler.handler);
        return write(linuxFileHandler->handler, data, size);
    }

    u64 FileSystem::ReadFile(FileHandler fileHandler, VoidPtr data, usize size)
    {
        LinuxFileHandler* linuxFileHandler = static_cast<LinuxFileHandler*>(fileHandler.handler);
        return read(linuxFileHandler->handler, data, size);
    }

    u64 FileSystem::ReadFileAt(FileHandler fileHandler, VoidPtr data, usize size, usize offset)
    {
        //https://stackoverflow.com/questions/19780919/read-write-from-file-descriptor-at-offset
        LinuxFileHandler* linuxFileHandler = static_cast<LinuxFileHandler*>(fileHandler.handler);
        return pread(linuxFileHandler->handler, data, size, offset);
    }

    FileHandler FileSystem::CreateFileMapping(FileHandler fileHandler, AccessMode accessMode, usize size)
    {
        return FileHandler{};
    }

    VoidPtr FileSystem::MapViewOfFile(FileHandler fileHandler)
    {
        return nullptr;
    }

    bool FileSystem::UnmapViewOfFile(VoidPtr map)
    {
        return false;
    }

    void FileSystem::CloseFileMapping(FileHandler fileHandler)
    {

    }

    void FileSystem::CloseFile(FileHandler fileHandler)
    {
        LinuxFileHandler* linuxFileHandler = static_cast<LinuxFileHandler*>(fileHandler.handler);

        close(linuxFileHandler->handler);

        MemoryGlobals::GetDefaultAllocator().DestroyAndFree(linuxFileHandler);
    }

}

#endif
