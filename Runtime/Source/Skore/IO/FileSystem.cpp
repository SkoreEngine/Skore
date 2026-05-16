#include "Skore/IO/FileSystem.hpp"

#include "Skore/Core/Span.hpp"


#include "Skore/IO/Path.hpp"
#include <filesystem>
#include "Skore/Core/Logger.hpp"

namespace fs = std::filesystem;

namespace Skore
{
	void FileSystemPlatformInit();

	namespace
	{
		char   homeDir[512] = {};
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
		FileSystemPlatformInit();
	}

	void FileSystemShutdown()
	{
	}

	void FileSystem::Reset()
	{
		tempFolder = String{};
	}
}