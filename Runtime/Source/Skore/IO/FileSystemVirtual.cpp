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

#ifdef SK_VIRTUAL_FILESYSTEM

#include "Path.hpp"
#include "FileSystemVirtual.hpp"
#include "Skore/Core/Allocator.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/UUID.hpp"
#include <chrono>
#include <cstring>

namespace Skore
{
	namespace
	{
		// Auto-initialize and shutdown the virtual file system
		struct FileSystemVirtualInitializer
		{
			FileSystemVirtualInitializer()
			{
				FileSystemVirtual::Initialize();
			}
			
			~FileSystemVirtualInitializer()
			{
				FileSystemVirtual::Shutdown();
			}
		};
		
		static FileSystemVirtualInitializer s_initializer;
		
		// Custom iterator handler for virtual directory navigation
		struct VirtualDirIterator
		{
			Array<String> entries;
			usize currentIndex = 0;
		};
		struct VirtualFile
		{
			Array<u8> data;
			u64 lastModifiedTime;
			bool isDirectory;
		};

		struct VirtualFileHandler
		{
			String path;
			AccessMode accessMode;
			usize position;
		};

		struct VirtualFileMapping
		{
			VoidPtr mappedMemory;
			usize size;
		};

		String s_currentDir = "/";
		String s_tempFolder;
		HashMap<String, VirtualFile> s_files;
		HashMap<VoidPtr, VirtualFileHandler> s_openFiles;
		HashMap<VoidPtr, VirtualFileMapping> s_fileMappings;
		Logger& logger = Logger::GetLogger("Skore::FileSystemVirtual");

		String NormalizePath(const StringView& path)
		{
			return path.StartsWith("/") ? String(path) : Path::Join("/", path);
		}
	}

	void FileSystemVirtual::Initialize()
	{
		s_currentDir = "/";
		s_files.Clear();
		s_openFiles.Clear();
		s_fileMappings.Clear();

		// Create root directory
		VirtualFile rootDir;
		rootDir.isDirectory = true;
		rootDir.lastModifiedTime = std::chrono::system_clock::now().time_since_epoch().count();
		s_files.Emplace("/", Traits::Move(rootDir));

		// Create assets directory
		String assetsDir = "/Assets";
		VirtualFile assetsFolder;
		assetsFolder.isDirectory = true;
		assetsFolder.lastModifiedTime = std::chrono::system_clock::now().time_since_epoch().count();
		s_files.Emplace(assetsDir, Traits::Move(assetsFolder));
	}

	void FileSystemVirtual::Shutdown()
	{
		// Close all open files
		for (auto it = s_openFiles.Begin(); it != s_openFiles.End(); ++it)
		{
			DestroyAndFree(static_cast<VirtualFileHandler*>(it->key));
		}
		s_openFiles.Clear();
		
		// Close all file mappings
		for (auto it = s_fileMappings.Begin(); it != s_fileMappings.End(); ++it)
		{
			VirtualFileMapping* mapping = static_cast<VirtualFileMapping*>(it->key);
			MemFree(mapping->mappedMemory);
			DestroyAndFree(mapping);
		}
		s_fileMappings.Clear();
		
		s_files.Clear();
	}

	bool FileSystemVirtual::AddVirtualFile(const StringView& path, const Array<u8>& data)
	{
		String normalizedPath = NormalizePath(path);
		
		// Check if parent directory exists
		String parent = Path::Parent(normalizedPath);
		if (!VirtualEntryExists(parent))
		{
			if (!AddVirtualDirectory(parent))
			{
				return false;
			}
		}
		
		// Create or update the file
		VirtualFile file;
		file.isDirectory = false;
		file.lastModifiedTime = std::chrono::system_clock::now().time_since_epoch().count();
		file.data = data;
		
		s_files.Emplace(normalizedPath, Traits::Move(file));
		return true;
	}

	bool FileSystemVirtual::AddVirtualFile(const StringView& path, const StringView& content)
	{
		Array<u8> data(content.Size());
		std::memcpy(data.begin(), content.Data(), content.Size());
		return AddVirtualFile(path, data);
	}

	bool FileSystemVirtual::AddVirtualDirectory(const StringView& path)
	{
		String normalizedPath = NormalizePath(path);
		
		// Check if the directory already exists
		if (VirtualEntryExists(normalizedPath))
		{
			auto it = s_files.Find(normalizedPath);
			return it->value.isDirectory;
		}
		
		// Create parent directories if needed
		String parent = Path::Parent(normalizedPath);
		if (parent != normalizedPath && !VirtualEntryExists(parent))
		{
			if (!AddVirtualDirectory(parent))
			{
				return false;
			}
		}
		
		// Create the directory
		VirtualFile dir;
		dir.isDirectory = true;
		dir.lastModifiedTime = std::chrono::system_clock::now().time_since_epoch().count();
		s_files.Emplace(normalizedPath, Traits::Move(dir));
		
		return true;
	}

	bool FileSystemVirtual::RemoveVirtualEntry(const StringView& path)
	{
		String normalizedPath = NormalizePath(path);
		
		// Check if the entry exists
		auto it = s_files.Find(normalizedPath);
		if (it == s_files.End())
		{
			return false;
		}
		
		// If it's a directory, remove all children
		if (it->value.isDirectory)
		{
			Array<String> toRemove;
			for (const auto& pair : s_files)
			{
				if (pair.key.StartsWith(normalizedPath) && pair.key != normalizedPath)
				{
					toRemove.EmplaceBack(pair.key);
				}
			}
			
			for (const auto& key : toRemove)
			{
				s_files.Remove(key);
			}
		}
		
		// Remove the entry itself
		s_files.Remove(normalizedPath);
		return true;
	}

	bool FileSystemVirtual::VirtualEntryExists(const StringView& path)
	{
		String normalizedPath = NormalizePath(path);
		return s_files.Find(normalizedPath) != s_files.End();
	}

	Array<String> FileSystemVirtual::ListVirtualEntries(const StringView& directory)
	{
		String normalizedPath = NormalizePath(directory);
		Array<String> entries;
		
		// Ensure the path ends with a slash for proper matching
		if (!normalizedPath.EndsWith("/"))
		{
			normalizedPath += "/";
		}
		
		for (const auto& pair : s_files)
		{
			// Skip the entry itself
			if (pair.key == normalizedPath || pair.key + "/" == normalizedPath)
			{
				continue;
			}
			
			// Check if this is a direct child of the directory
			if (pair.key.StartsWith(normalizedPath))
			{
				StringView remaining = StringView(pair.key).Substr(normalizedPath.Size());
				usize slashPos = remaining.Find('/');
				
				// If there's no slash or the slash is at the end, it's a direct child
				if (slashPos == String::nPos || slashPos == remaining.Size() - 1)
				{
					// Remove trailing slash if present
					if (slashPos == remaining.Size() - 1)
					{
						entries.EmplaceBack(String(remaining.Substr(0, slashPos)));
					}
					else
					{
						entries.EmplaceBack(String(remaining));
					}
				}
			}
		}
		
		return entries;
	}

	// Implementation of FileSystem static methods

	void FileSystem::SetupTempFolder(StringView tempFolder)
	{
		s_tempFolder = tempFolder;
		if (!s_tempFolder.Empty())
		{
			FileSystem::Remove(s_tempFolder);
			FileSystem::CreateDirectory(s_tempFolder);
		}
	}

	String FileSystem::CurrentDir()
	{
		return s_currentDir;
	}

	String FileSystem::DocumentsDir()
	{
		return "/Documents";
	}

	String FileSystem::AppFolder()
	{
		return "/App";
	}

	String FileSystem::AssetFolder()
	{
		return "/Assets";
	}

	String FileSystem::TempFolder()
	{
		return s_tempFolder;
	}

	FileStatus FileSystem::GetFileStatus(const StringView& path)
	{
		String normalizedPath = NormalizePath(path);
		
		auto it = s_files.Find(normalizedPath);
		if (it != s_files.End())
		{
			const VirtualFile& file = it->value;
			return {
				.exists = true,
				.isDirectory = file.isDirectory,
				.lastModifiedTime = file.lastModifiedTime,
				.fileSize = file.data.Size(),
				.fileId = HashValue(normalizedPath.CStr())
			};
		}

		return {
			.exists = false,
			.isDirectory = false,
			.lastModifiedTime = 0,
			.fileSize = 0,
			.fileId = 0
		};
	}

	u64 FileSystem::GetFileSize(const StringView& path)
	{
		String normalizedPath = NormalizePath(path);
		
		auto it = s_files.Find(normalizedPath);
		if (it != s_files.End())
		{
			return it->value.data.Size();
		}

		return 0;
	}

	bool FileSystem::CreateDirectory(const StringView& path)
	{
		return FileSystemVirtual::AddVirtualDirectory(path);
	}

	bool FileSystem::Remove(const StringView& path)
	{
		return FileSystemVirtual::RemoveVirtualEntry(path);
	}

	bool FileSystem::Rename(const StringView& oldName, const StringView& newName)
	{
		String oldPath = NormalizePath(oldName);
		String newPath = NormalizePath(newName);
		
		// Check if the source exists
		auto srcIt = s_files.Find(oldPath);
		if (srcIt == s_files.End())
		{
			return false;
		}

		// Ensure parent directory of the destination exists
		String parent = Path::Parent(newPath);
		if (!FileSystemVirtual::VirtualEntryExists(parent))
		{
			if (!FileSystemVirtual::AddVirtualDirectory(parent))
			{
				return false;
			}
		}

		// Create the new entry
		s_files.Emplace(newPath, Traits::Move(srcIt->value));
		
		// Remove the old entry
		s_files.Remove(oldPath);
		
		return true;
	}

	bool FileSystem::CopyFile(const StringView& from, const StringView& to)
	{
		String fromPath = NormalizePath(from);
		String toPath = NormalizePath(to);
		
		// Check if the source exists and is a file
		auto srcIt = s_files.Find(fromPath);
		if (srcIt == s_files.End() || srcIt->value.isDirectory)
		{
			return false;
		}

		// Create the destination file
		return FileSystemVirtual::AddVirtualFile(toPath, srcIt->value.data);
	}

	FileHandler FileSystem::OpenFile(const StringView& path, AccessMode accessMode)
	{
		String normalizedPath = NormalizePath(path);
		
		// Create parent directories if needed for write operations
		if (accessMode == AccessMode::WriteOnly || accessMode == AccessMode::ReadAndWrite)
		{
			String parent = Path::Parent(normalizedPath);
			if (!FileSystemVirtual::VirtualEntryExists(parent))
			{
				if (!FileSystemVirtual::AddVirtualDirectory(parent))
				{
					return {};
				}
			}
		}

		// Check if the file exists
		auto it = s_files.Find(normalizedPath);
		bool fileExists = (it != s_files.End() && !it->value.isDirectory);

		// If it doesn't exist and we're trying to read, fail
		if (accessMode == AccessMode::ReadOnly && !fileExists)
		{
			return {};
		}

		// Create or truncate the file if writing
		if (accessMode == AccessMode::WriteOnly || accessMode == AccessMode::ReadAndWrite)
		{
			if (!fileExists)
			{
				// Create a new file
				VirtualFile newFile;
				newFile.isDirectory = false;
				newFile.lastModifiedTime = std::chrono::system_clock::now().time_since_epoch().count();
				s_files.Emplace(normalizedPath, Traits::Move(newFile));
			}
			else if (accessMode == AccessMode::WriteOnly)
			{
				// Truncate an existing file
				it->value.data.Clear();
				it->value.lastModifiedTime = std::chrono::system_clock::now().time_since_epoch().count();
			}
		}

		// Create a file handler
		VirtualFileHandler* handler = Alloc<VirtualFileHandler>();
		handler->path = normalizedPath;
		handler->accessMode = accessMode;
		handler->position = 0;
		
		// Store the handler
		s_openFiles.Emplace(handler, *handler);
		
		return {handler};
	}

	u64 FileSystem::GetFileSize(FileHandler fileHandler)
	{
		VirtualFileHandler* handler = static_cast<VirtualFileHandler*>(fileHandler.handler);
		if (!handler)
		{
			return 0;
		}

		auto it = s_files.Find(handler->path);
		if (it != s_files.End())
		{
			return it->value.data.Size();
		}

		return 0;
	}

	u64 FileSystem::WriteFile(FileHandler fileHandler, ConstPtr data, usize size)
	{
		VirtualFileHandler* handler = static_cast<VirtualFileHandler*>(fileHandler.handler);
		if (!handler || (handler->accessMode != AccessMode::WriteOnly && handler->accessMode != AccessMode::ReadAndWrite))
		{
			return 0;
		}

		auto it = s_files.Find(handler->path);
		if (it == s_files.End())
		{
			return 0;
		}

		VirtualFile& file = it->value;
		
		// Resize the array if needed
		if (handler->position + size > file.data.Size())
		{
			file.data.Resize(handler->position + size);
		}
		
		// Copy the data
		std::memcpy(file.data.begin() + handler->position, data, size);
		
		// Update position and last modified time
		handler->position += size;
		file.lastModifiedTime = std::chrono::system_clock::now().time_since_epoch().count();
		
		return size;
	}
	
	// Implementation of DirIterator for virtual file system
	DirIterator::DirIterator(const StringView& directory) : m_directory(directory), m_handler(nullptr)
	{
		String normalizedPath = NormalizePath(directory);
		
		// Check if the directory exists
		auto it = s_files.Find(normalizedPath);
		if (it == s_files.End() || !it->value.isDirectory)
		{
			return;  // Invalid directory or not a directory
		}
		
		// Normalize path to ensure it ends with a slash
		if (!normalizedPath.EndsWith("/"))
		{
			normalizedPath += "/";
		}
		
		// Collect all direct children of this directory
		VirtualDirIterator* handler = Alloc<VirtualDirIterator>();
		
		for (const auto& pair : s_files)
		{
			// Skip the directory itself
			if (pair.key == normalizedPath || pair.key == normalizedPath.SubStr(0, normalizedPath.Size() - 1))
			{
				continue;
			}
			
			// Check if this is a direct child of the directory
			if (pair.key.StartsWith(normalizedPath))
			{
				StringView remaining = StringView(pair.key).Substr(normalizedPath.Size());
				usize slashPos = remaining.Find('/');
				
				// If there's no slash or the slash is at the end, it's a direct child
				if (slashPos == String::nPos || slashPos == remaining.Size() - 1)
				{
					String entryPath;
					
					// Remove trailing slash if present
					if (slashPos == remaining.Size() - 1)
					{
						entryPath = Path::Join(directory, String(remaining.Substr(0, slashPos)));
					}
					else
					{
						entryPath = Path::Join(directory, String(remaining));
					}
					
					handler->entries.EmplaceBack(entryPath);
				}
			}
		}
		
		// Sort entries for consistent iteration order
		std::sort(handler->entries.begin(), handler->entries.end());
		
		if (!handler->entries.Empty())
		{
			m_path = handler->entries[0];
			m_handler = handler;
		}
		else
		{
			DestroyAndFree(handler);
		}
	}
	
	DirIterator& DirIterator::operator++()
	{
		if (m_handler)
		{
			VirtualDirIterator* handler = static_cast<VirtualDirIterator*>(m_handler);
			
			// Move to the next entry
			handler->currentIndex++;
			
			if (handler->currentIndex < handler->entries.Size())
			{
				m_path = handler->entries[handler->currentIndex];
				return *this;
			}
			
			// No more entries, clean up
			DestroyAndFree(handler);
			m_handler = nullptr;
		}
		
		m_path.Clear();
		return *this;
	}
	
	DirIterator::~DirIterator()
	{
		if (m_handler)
		{
			DestroyAndFree(static_cast<VirtualDirIterator*>(m_handler));
		}
	}

	u64 FileSystem::ReadFile(FileHandler fileHandler, VoidPtr data, usize size)
	{
		VirtualFileHandler* handler = static_cast<VirtualFileHandler*>(fileHandler.handler);
		if (!handler || (handler->accessMode != AccessMode::ReadOnly && handler->accessMode != AccessMode::ReadAndWrite))
		{
			return 0;
		}

		auto it = s_files.Find(handler->path);
		if (it == s_files.End())
		{
			return 0;
		}

		const VirtualFile& file = it->value;
		
		// Calculate how much we can actually read
		usize available = file.data.Size() - handler->position;
		usize bytesToRead = (size < available) ? size : available;
		
		if (bytesToRead > 0)
		{
			// Copy the data
			std::memcpy(data, file.data.begin() + handler->position, bytesToRead);
			
			// Update position
			handler->position += bytesToRead;
		}
		
		return bytesToRead;
	}

	u64 FileSystem::ReadFileAt(FileHandler fileHandler, VoidPtr data, usize size, usize offset)
	{
		VirtualFileHandler* handler = static_cast<VirtualFileHandler*>(fileHandler.handler);
		if (!handler || (handler->accessMode != AccessMode::ReadOnly && handler->accessMode != AccessMode::ReadAndWrite))
		{
			return 0;
		}

		auto it = s_files.Find(handler->path);
		if (it == s_files.End())
		{
			return 0;
		}

		const VirtualFile& file = it->value;
		
		// Check if offset is valid
		if (offset >= file.data.Size())
		{
			return 0;
		}
		
		// Calculate how much we can actually read
		usize available = file.data.Size() - offset;
		usize bytesToRead = (size < available) ? size : available;
		
		if (bytesToRead > 0)
		{
			// Copy the data
			std::memcpy(data, file.data.begin() + offset, bytesToRead);
		}
		
		return bytesToRead;
	}

	void FileSystem::CloseFile(FileHandler fileHandler)
	{
		VirtualFileHandler* handler = static_cast<VirtualFileHandler*>(fileHandler.handler);
		if (handler)
		{
			s_openFiles.Remove(handler);
			DestroyAndFree(handler);
		}
	}

	FileHandler FileSystem::CreateFileMapping(FileHandler fileHandler, AccessMode accessMode, usize size)
	{
		VirtualFileHandler* handler = static_cast<VirtualFileHandler*>(fileHandler.handler);
		if (!handler)
		{
			return {};
		}

		auto it = s_files.Find(handler->path);
		if (it == s_files.End())
		{
			return {};
		}

		VirtualFile& file = it->value;
		
		// Resize the file if needed
		if (size > file.data.Size())
		{
			file.data.Resize(size);
		}
		
		// Create a mapping
		VirtualFileMapping* mapping = Alloc<VirtualFileMapping>();
		mapping->size = file.data.Size();
		
		// Create a copy of the data for the mapping
		u8* mappedMemory = static_cast<u8*>(MemAlloc(file.data.Size()));
		std::memcpy(mappedMemory, file.data.begin(), file.data.Size());
		mapping->mappedMemory = mappedMemory;
		
		// Store the mapping
		s_fileMappings.Emplace(mapping, *mapping);
		
		return {mapping};
	}

	VoidPtr FileSystem::MapViewOfFile(FileHandler fileHandler)
	{
		VirtualFileMapping* mapping = static_cast<VirtualFileMapping*>(fileHandler.handler);
		if (!mapping)
		{
			return nullptr;
		}

		return mapping->mappedMemory;
	}

	bool FileSystem::UnmapViewOfFile(VoidPtr map)
	{
		// Find the mapping that has this memory
		for (const auto& pair : s_fileMappings)
		{
			if (pair.value.mappedMemory == map)
			{
				return true;
			}
		}
		
		return false;
	}

	void FileSystem::CloseFileMapping(FileHandler fileHandler)
	{
		VirtualFileMapping* mapping = static_cast<VirtualFileMapping*>(fileHandler.handler);
		if (mapping)
		{
			// Free the mapped memory
			MemFree(mapping->mappedMemory);
			
			// Remove the mapping
			s_fileMappings.Remove(mapping);
			
			// Free the mapping structure
			DestroyAndFree(mapping);
		}
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
		if (FileHandler fileHandler = OpenFile(path, AccessMode::WriteOnly))
		{
			WriteFile(fileHandler, string.Data(), string.Size());
			CloseFile(fileHandler);
		}
	}

	void FileSystem::Reset()
	{
		s_tempFolder = String{};
		
		// Close all open files
		for (auto it = s_openFiles.Begin(); it != s_openFiles.End(); ++it)
		{
			DestroyAndFree(static_cast<VirtualFileHandler*>(it->key));
		}
		s_openFiles.Clear();
		
		// Close all file mappings
		for (auto it = s_fileMappings.Begin(); it != s_fileMappings.End(); ++it)
		{
			VirtualFileMapping* mapping = static_cast<VirtualFileMapping*>(it->key);
			MemFree(mapping->mappedMemory);
			DestroyAndFree(mapping);
		}
		s_fileMappings.Clear();
		
		s_files.Clear();
		
		// Initialize the virtual file system again
		FileSystemVirtual::Initialize();
	}
}

#endif
