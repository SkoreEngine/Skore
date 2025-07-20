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

#include "Skore/Core/Span.hpp"


#include "Path.hpp"
#include <filesystem>
#include "Skore/Core/Logger.hpp"

namespace fs = std::filesystem;

namespace Skore
{
    namespace
    {
        char homeDir[512] = {};
        String tempFolder;

        Logger& logger = Logger::GetLogger("Skore::FileSystem");
    }

    void FileSystem::SetupTempFolder(StringView tempFolder_)
    {
        tempFolder = tempFolder_;
        if (!tempFolder.Empty())
        {
            Remove(tempFolder);
            CreateDirectory(tempFolder);
        }
    }

    String FileSystem::TempFolder()
    {
        return tempFolder;
    }

    bool FileSystem::CreateDirectory(const StringView& path)
    {
        return fs::create_directories(fs::path(path.begin(), path.end()));
    }

    bool FileSystem::Remove(const StringView& path, bool printException)
    {
        try
        {
            return fs::remove_all(path.CStr());
        }
        catch (std::exception& ex)
        {
            if (printException)
            {
                logger.Error("error on remove {} error: {} ", path, ex.what());
            }
        }
        return false;
    }

    bool FileSystem::Rename(const StringView& oldName, const StringView& newName)
    {
        if (!GetFileStatus(Path::Parent(newName)).exists)
        {
            CreateDirectory(Path::Parent(newName));
        }

        std::error_code ec{};
        fs::rename(oldName.CStr(), newName.CStr(), ec);
        return ec.value() == 0;
    }

    bool FileSystem::CopyFile(const StringView& from, const StringView& to)
    {
        auto            toPath = fs::path(to.begin(), to.end());
        const auto      copyOptions = fs::copy_options::update_existing | fs::copy_options::recursive;
        std::error_code ec{};
        fs::copy(fs::path(from.begin(), from.end()), toPath, copyOptions, ec);
        return ec.value() == 0;
    }

    String FileSystem::ReadFileAsString(const StringView& path)
    {
        String ret{};
        if (FileHandler fileHandler = OpenFile(path, AccessMode::ReadOnly))
        {
            ret.Resize(GetFileSize(fileHandler));
            ReadFile(fileHandler, ret.begin(), ret.Size());
            CloseFile(fileHandler);
        }
        return ret;
    }

    void FileSystem::ReadFileAsByteArray(const StringView& path, Array<u8>& buffer)
    {
        if (FileHandler fileHandler = OpenFile(path, AccessMode::ReadOnly))
        {
            buffer.Resize(GetFileSize(fileHandler));
            ReadFile(fileHandler, buffer.begin(), buffer.Size());
            CloseFile(fileHandler);
        }
    }

    void FileSystem::SaveFileAsByteArray(const StringView& path, Span<u8> bytes)
    {
        if (!GetFileStatus(Path::Parent(path)).exists)
        {
            CreateDirectory(Path::Parent(path));
        }

        if (FileHandler fileHandler = OpenFile(path, AccessMode::WriteOnly))
        {
            WriteFile(fileHandler, bytes.Data(), bytes.Size());
            CloseFile(fileHandler);
        }
    }

    void FileSystem::SaveFileAsString(const StringView& path, const StringView& string)
    {
        if (!GetFileStatus(Path::Parent(path)).exists)
        {
            CreateDirectory(Path::Parent(path));
        }

        if (FileHandler fileHandler = OpenFile(path, AccessMode::WriteOnly))
        {
            WriteFile(fileHandler, string.Data(), string.Size());
            CloseFile(fileHandler);
        }
    }

    void FileSystemInit()
    {
        if (!tempFolder.Empty())
        {
            FileSystem::Remove(tempFolder);
            FileSystem::CreateDirectory(tempFolder);
        }
    }

    void FileSystemShutdown()
    {
        if (!tempFolder.Empty())
        {
            FileSystem::Remove(tempFolder);
        }
    }

    void FileSystem::Reset()
    {
        tempFolder = String{};
    }
}
