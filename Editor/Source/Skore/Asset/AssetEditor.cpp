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

#include "AssetEditor.hpp"

#include <iostream>
#include <mutex>

#include "AssetFile.hpp"
#include "AssetTypes.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"

#include <optional>

#include "SDL3/SDL_filesystem.h"
#include "SDL3/SDL_loadso.h"
#include "Skore/App.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/HashSet.hpp"
#include "Skore/Core/Ref.hpp"
#include "Skore/Core/StaticContent.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/IO/FileWatcher.hpp"

namespace Skore
{
	void ReflectionSetReadOnly(bool readOnly);

	namespace
	{
		Logger&           logger = Logger::GetLogger("Skore::AssetEditor", LogLevel::Debug);
		AssetFile*        projectFile = nullptr;
		SDL_SharedObject* projectPlugin = nullptr;
		Array<AssetFile*> packages;
		String            libFolder = {};

		HashMap<TypeID, Array<AssetFile*>> assetsByType;

		std::mutex                  byPathMutex;
		HashMap<String, AssetFile*> assetsByPath;

		Array<Ref<AssetHandler>>            handlers;
		HashMap<String, Ref<AssetHandler>>  handlersByExtension;
		HashMap<TypeID, Ref<AssetHandler>>  handlersByTypeID;
		HashMap<String, Ref<AssetImporter>> extensionImporters;
		HashSet<String>                     ignoredExtensions;

		GPUTexture* directoryThumbnail = nullptr;
		GPUTexture* fileThumbnail = nullptr;

		FileWatcher       fileWatcher;
	}

	void RemoveFileByAbsolutePath(StringView path)
	{
		std::lock_guard lock(byPathMutex);
		assetsByPath.Erase(path);
	}

	void AddFileByAbsolutePath(StringView path, AssetFile* file)
	{
		std::lock_guard lock(byPathMutex);
		assetsByPath.Insert(path, file);
	}

	void AssetEditor::SetProject(StringView name, StringView directory)
	{
		libFolder = String(directory).Append(SK_PATH_SEPARATOR).Append("Library");
		if (!FileSystem::GetFileStatus(libFolder).exists)
		{
			FileSystem::CreateDirectory(libFolder);
		}

		String projectPluginPath = Path::Join(directory, "Binaries", String(name).Append(SK_SHARED_EXT));
		if (FileSystem::GetFileStatus(projectPluginPath).exists)
		{
			projectPlugin = SDL_LoadObject(projectPluginPath.CStr());
			if (projectPlugin)
			{
				if (auto func = SDL_LoadFunction(projectPlugin, "SkoreLoadPlugin"))
				{
					ReflectionSetReadOnly(false);
					func();
					ReflectionSetReadOnly(true);
				}
			}
		}


		String libAssets = Path::Join(directory, "Assets");
		projectFile = ScanForAssets(nullptr, name, libAssets);
	}

	void AssetEditor::AddPackage(StringView name, StringView directory)
	{
		packages.EmplaceBack(ScanForAssets(nullptr, name, directory));
	}

	struct AssetsToScan
	{
		String     path;
		String     absolutePath;
		AssetFile* parent{};
	};

	Span<AssetFile*> AssetEditor::GetPackages()
	{
		return packages;
	}

	AssetFile* AssetEditor::CreateDirectory(AssetFile* parent)
	{
		SK_ASSERT(parent, "parent cannot be null");

		if (!parent)
		{
			return nullptr;
		}

		AssetFile* newDirectory = MemoryGlobals::GetDefaultAllocator()->Alloc<AssetFile>();

		newDirectory->m_fileName = CreateUniqueName(parent, "New Folder");
		newDirectory->m_type = AssetFileType::Directory;
		newDirectory->m_currentVersion = 1;
		newDirectory->m_persistedVersion = 0;
		newDirectory->m_parent = parent;

		AddAssetFile(newDirectory);

		parent->m_children.EmplaceBack(newDirectory);

		newDirectory->UpdatePath();
		newDirectory->Save();

		fileWatcher.Watch(newDirectory, newDirectory->GetAbsolutePath());

		return newDirectory;
	}

	AssetFile* AssetEditor::FindOrCreateAsset(AssetFile* parent, TypeID typeId, StringView suggestedName)
	{
		SK_ASSERT(parent, "parent cannot be null");
		if (!parent) return nullptr;

		if (auto it = handlersByTypeID.Find(typeId))
		{
			String assetName = suggestedName.Empty() ? String("New ").Append(it->second->Name()) : String(suggestedName);
			for (AssetFile* child : parent->m_children)
			{
				if (child->m_fileName == assetName && child->GetAssetTypeId() == typeId)
				{
					return child;
				}
			}
		}
		return CreateAsset(parent, typeId, suggestedName);
	}

	AssetFile* AssetEditor::CreateAsset(AssetFile* parent, TypeID typeId, StringView suggestedName, UUID uuid)
	{
		SK_ASSERT(parent, "parent cannot be null");
		if (!parent) return nullptr;

		if (auto it = handlersByTypeID.Find(typeId))
		{
			String assetName = suggestedName.Empty() ? String("New ").Append(it->second->Name()) : String(suggestedName);
			assetName = CreateUniqueName(parent, assetName);

			AssetFile* newAsset = CreateAssetFile(parent, assetName, it->second->Extension());
			newAsset->m_type = parent->m_type == AssetFileType::Directory || parent->m_type == AssetFileType::Root ? AssetFileType::Asset : AssetFileType::Child;
			newAsset->m_currentVersion = 1;
			newAsset->m_persistedVersion = 0;
			newAsset->m_parent = parent;
			newAsset->m_handler = it->second.Get();
			newAsset->m_uuid = !uuid ? UUID::RandomUUID() : uuid;

			AddAssetFile(newAsset);

			Assets::Register(newAsset->m_path, newAsset->m_uuid, newAsset);

			logger.Debug("asset {} created  ", assetName);

			return newAsset;
		}

		SK_ASSERT(false, "handler not found");

		return nullptr;
	}

	String AssetEditor::CreateUniqueName(AssetFile* parent, StringView desiredName)
	{
		if (parent == nullptr) return {};

		u32    count{};
		String finalName = desiredName;
		bool   nameFound;
		do
		{
			nameFound = true;
			for (AssetFile* child : parent->m_children)
			{
				if (finalName == child->m_fileName)
				{
					finalName = desiredName;
					finalName += " (";
					finalName.Append(ToString(++count));
					finalName += ")";
					nameFound = false;
					break;
				}
			}
		}
		while (!nameFound);
		return finalName;
	}

	void AssetEditor::GetUpdatedAssets(Array<AssetFile*>& updatedAssets)
	{
		Queue<AssetFile*> pendingDirectories;
		AssetFile*        current = nullptr;

		current = projectFile;

		while (current != nullptr)
		{
			for (AssetFile* child : current->m_children)
			{
				if (child->IsDirectory())
				{
					pendingDirectories.Enqueue(child);
				}

				if (child->IsDirty())
				{
					updatedAssets.EmplaceBack(child);
				}
			}

			current = nullptr;
			if (!pendingDirectories.IsEmpty())
			{
				current = pendingDirectories.Dequeue();
			}
		}
	}

	Span<AssetFile*> AssetEditor::GetAssetsByType(TypeID typeId)
	{
		if (auto it = assetsByType.Find(typeId))
		{
			return it->second;
		}
		return {};
	}

	void AssetEditor::ImportFile(AssetFile* parent, StringView path)
	{
		FileSystem::CopyFile(path, parent->GetAbsolutePath());
	}

	AssetFile* AssetEditor::GetFileByAbsolutePath(StringView path)
	{
		std::lock_guard lock(byPathMutex);
		if (auto it = assetsByPath.Find(path))
		{
			return it->second;
		}
		return nullptr;
	}

	StringView AssetEditor::GetLibFolder()
	{
		return libFolder;
	}

	GPUTexture* AssetEditor::GetDirectoryThumbnail()
	{
		return directoryThumbnail;
	}

	GPUTexture* AssetEditor::GetFileThumbnail()
	{
		return fileThumbnail;
	}

	AssetFile* AssetEditor::GetProject()
	{
		return projectFile;
	}

	AssetFile* AssetEditor::CreateAssetFile(AssetFile* parent, StringView name, StringView extension)
	{
		AssetFile* file = Alloc<AssetFile>();

		file->m_fileName = name;
		file->m_extension = extension;

		if (parent)
		{
			parent->AddChild(file);
			if (parent->IsDirectory())
			{
				file->m_path = String(parent->GetPath()).Append("/").Append(name).Append(file->m_extension);
			}
			else
			{
				file->m_path = String(parent->GetPath()).Append("#").Append(name).Append(file->m_extension);
			}
		}
		else
		{
			file->m_path = String(name).Append(":/");
		}

		logger.Debug("asset registered relative path '{}'", file->m_path);

		return file;
	}

	void AssetEditor::AddAssetFile(AssetFile* assetFile)
	{
		if (assetFile->m_handler && assetFile->m_handler->GetAssetTypeId() != 0)
		{
			auto it = assetsByType.Find(assetFile->m_handler->GetAssetTypeId());
			if (it == assetsByType.end())
			{
				it = assetsByType.Emplace(assetFile->m_handler->GetAssetTypeId(), Array<AssetFile*>{}).first;
			}
			it->second.EmplaceBack(assetFile);
		}
	}

	void AssetEditor::RemoveAssetFile(AssetFile* assetFile)
	{
		if (assetFile->GetHandler() && assetFile->GetHandler()->GetAssetTypeId() != 0)
		{
			auto it = assetsByType.Find(assetFile->GetHandler()->GetAssetTypeId());
			if (it != assetsByType.end())
			{
				if (auto itAsset = FindFirst(it->second.begin(), it->second.end(), assetFile))
				{
					it->second.Erase(itAsset);
				}
			}
		}
	}

	void AssetEditor::AssetEditorOnUpdate()
	{
		fileWatcher.CheckForUpdates([](const FileWatcherModified& fileWatcherModified)
		{
			switch (fileWatcherModified.event)
			{
				case FileNotifyEvent::Added:
					ScanForAssets(static_cast<AssetFile*>(fileWatcherModified.userData), fileWatcherModified.name, fileWatcherModified.path);
					break;
				case FileNotifyEvent::Removed:
					//static_cast<AssetFile*>(fileWatcherModified.userData)->Delete();
					break;
				case FileNotifyEvent::Modified:
					//TODO
					break;
				case FileNotifyEvent::Renamed:
					//static_cast<AssetFile*>(fileWatcherModified.userData)->Rename(fileWatcherModified.name);
					break;
			}
		});
	}

	AssetFile* AssetEditor::ScanForAssets(AssetFile* parent, StringView name, StringView path)
	{
		Queue<AssetsToScan> pendingItems(100);

		std::optional<AssetsToScan> currentScanItem = std::make_optional(AssetsToScan{
			.absolutePath = path,
			.parent = parent
		});

		AssetFile* first = nullptr;

		while (currentScanItem.has_value())
		{
			const AssetsToScan& item = currentScanItem.value();
			StringView extension = Path::Extension(item.absolutePath);

			do
			{
				if (extension == SK_IMPORT_EXTENSION ||
					extension == SK_INFO_EXTENSION ||
					extension == SK_PROJECT_EXTENSION)
				{
					break;
				}

				if (ignoredExtensions.Has(extension))
				{
					break;
				}

				if (GetFileByAbsolutePath(item.absolutePath))
				{
					break;
				}

				FileStatus status = FileSystem::GetFileStatus(item.absolutePath);
				AssetFile* assetFile = CreateAssetFile(item.parent, item.parent != nullptr ? Path::Name(item.absolutePath) : String(name), extension);
				if (first == nullptr)
				{
					first = assetFile;
				}

				assetFile->UpdateAbsolutePath(item.absolutePath);
				fileWatcher.Watch(assetFile, item.absolutePath);

				if (!status.isDirectory)
				{
					if (const auto& it = handlersByExtension.Find(assetFile->m_extension))
					{
						assetFile->m_type = AssetFileType::Asset;
						assetFile->m_handler = it->second.Get();
					}
					else if (const auto& it = extensionImporters.Find(extension))
					{
						assetFile->m_type = AssetFileType::ImportedAsset;
						assetFile->m_importer = it->second.Get();

						if (const auto& itHandler = handlersByTypeID.Find(assetFile->m_importer->GetAssetTypeId()))
						{
							assetFile->m_handler = itHandler->second.Get();
						}
					}
					else
					{
						assetFile->m_type = AssetFileType::Other;
					}

					AddAssetFile(assetFile);
					assetFile->Register();
				}

				if (status.isDirectory)
				{
					assetFile->m_type = AssetFileType::Directory;

					for (const String& entry : DirectoryEntries(item.absolutePath))
					{
						pendingItems.Enqueue(AssetsToScan{
							.absolutePath = entry,
							.parent = assetFile
						});
					}
				}
			}
			while (false);

			currentScanItem.reset();
			if (!pendingItems.IsEmpty())
			{
				currentScanItem = std::make_optional(pendingItems.Dequeue());
			}
		}
		return first;
	}

	void AssetEditorInit()
	{
		for (TypeID derivedId : Reflection::GetDerivedTypes(TypeInfo<AssetHandler>::ID()))
		{
			if (ReflectType* type = Reflection::FindTypeById(derivedId))
			{
				Ref<AssetHandler> handler = StaticPointerCast<AssetHandler>(Ref{type->NewObject(), MemoryGlobals::GetDefaultAllocator()});

				if (!handler)
				{
					logger.Debug("handler {} cannot be instantiated ", type->GetName());
					continue;
				}

				handlers.PushBack(handler);

				if (String extension = handler->Extension(); !extension.Empty())
				{
					logger.Debug("registered handler {} for extension {} ", type->GetName(), extension);
					handlersByExtension.Insert(extension, handler);
				}

				for (const String& extension : handler->AssociatedExtensions())
				{
					ignoredExtensions.Insert(extension);
				}

				if (TypeID typeId = handler->GetAssetTypeId(); typeId != 0)
				{
					handlersByTypeID.Insert(typeId, handler);
				}
			}
		}

		for (TypeID derivedId : Reflection::GetDerivedTypes(TypeInfo<AssetImporter>::ID()))
		{
			if (ReflectType* type = Reflection::FindTypeById(derivedId))
			{
				Ref<AssetImporter> importer = StaticPointerCast<AssetImporter>(Ref{type->NewObject(), MemoryGlobals::GetDefaultAllocator()});

				for (const String& extension : importer->ImportExtensions())
				{
					logger.Debug("registered importer {} for extension {} ", type->GetName(), extension);
					extensionImporters.Insert(extension, importer);
				}

				for (const String& extension : importer->AssociatedExtensions())
				{
					ignoredExtensions.Insert(extension);
				}
			}
		}

		directoryThumbnail = StaticContent::GetTexture("Content/Images/FolderIcon.png");
		fileThumbnail = StaticContent::GetTexture("Content/Images/FileIcon.png");

		Event::Bind<OnUpdate, AssetEditor::AssetEditorOnUpdate>();

		fileWatcher.Start();
	}

	void AssetEditorShutdown()
	{
		fileWatcher.Stop();

		if (projectPlugin)
		{
			SDL_UnloadObject(projectPlugin);
		}

		Event::Unbind<OnUpdate, AssetEditor::AssetEditorOnUpdate>();

		directoryThumbnail->Destroy();
		fileThumbnail->Destroy();

		DestroyAndFree(projectFile);

		projectFile = nullptr;

		extensionImporters.Clear();
		handlers.Clear();
		handlersByExtension.Clear();
		handlersByTypeID.Clear();
	}
}
