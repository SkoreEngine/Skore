#include "FileSystem.hpp"
#include "Path.hpp"

#include <filesystem>

#include "Skore/Core/Logger.hpp"
namespace fs = std::filesystem;

namespace Skore
{
    namespace
    {
        char homeDir[512] = {};

        Logger& logger = Logger::GetLogger("Skore::FileSystem");
    }

    String FileSystem::AssetFolder()
    {
#ifdef SK_DEV_ASSETS_PATH
        return SK_DEV_ASSETS_PATH;
#endif

        String currentDir = CurrentDir();
        String assetDir = Path::Join(currentDir, "Assets");
        while (!GetFileStatus(assetDir).exists)
        {
            currentDir = Path::Parent(currentDir);
            if (!GetFileStatus(currentDir).exists)
            {
                return CurrentDir();
            }
            assetDir = Path::Join(currentDir, "Assets");
        }
        return assetDir;
    }

    String FileSystem::TempFolder()
    {
        return {};
    }

    bool FileSystem::CreateDirectory(const StringView& path)
    {
        return fs::create_directories(fs::path(path.begin(), path.end()));
    }

    bool FileSystem::Remove(const StringView& path)
    {
        try
        {
            return fs::remove_all(path.CStr());
        }
        catch (std::exception& ex)
        {
            logger.Error("error on remove {} error: {} ", path, ex.what());
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

    Array<u8> FileSystem::ReadFileAsByteArray(const StringView& path)
    {
        Array<u8> ret{};
        if (FileHandler fileHandler = OpenFile(path, AccessMode::ReadOnly))
        {
            ret.Resize(GetFileSize(fileHandler));
            ReadFile(fileHandler, ret.begin(), ret.Size());
            CloseFile(fileHandler);
        }
        return ret;
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

    OutputFileStream::OutputFileStream(StringView file)
    {
        stream.open(file.CStr(), std::ios::out | std::ios::binary);
    }

    usize OutputFileStream::Write(u8* data, usize size)
    {
        auto offset = streamSize;
        stream.write(reinterpret_cast<const char*>(data), size);
        streamSize += size;
        return offset;
    }

    void OutputFileStream::Close()
    {
        stream.close();
    }
}
