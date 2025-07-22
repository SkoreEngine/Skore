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

#include "ResourceAssets.hpp"

#include <optional>
#include <queue>

#include "SDL3/SDL_misc.h"
#include "Skore/Editor.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/FileTypes.hpp"
#include "Skore/IO/FileWatcher.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceType.hpp"
#include "Skore/Scene/SceneCommon.hpp"

namespace Skore
{
	namespace
	{
		struct AssetsPendingImport
		{
			RID    parent;
			String path;
		};


		Array<ResourceAssetHandler*>  handlers;
		Array<ResourceAssetImporter*> importers;
		HashMap<String, String> loadedPackages;

		HashMap<String, ResourceAssetHandler*> handlersByExtension;
		HashMap<TypeID, ResourceAssetHandler*> handlersByTypeID;

		HashMap<String, ResourceAssetImporter*> importersByExtension;

		Array<AssetsPendingImport> pendingImports;

		FileWatcher fileWatcher;

		Allocator* alloc = MemoryGlobals::GetHeapAllocator();


		Logger& logger = Logger::GetLogger("Skore::ResourceAssets", LogLevel::Debug);

		String GetNewAbsolutePath(RID asset)
		{
			String result = {};
			RID    current = asset;
			while (true)
			{
				ResourceObject assetObject = Resources::Read(current);
				result = String("/").Append(assetObject.GetString(ResourceAsset::Name)).Append(assetObject.GetString(ResourceAsset::Extension)).Append(result);

				ResourceStorage* storage = Resources::GetStorage(current);

				if (storage->parentFieldIndex == ResourceAssetDirectory::DirectoryAsset)
				{
					current = storage->parent->rid;
				}

				RID parent = Resources::GetParent(current);
				if (Resources::GetStorage(Resources::GetParent(parent))->resourceType->GetID() == TypeInfo<ResourceAssetPackage>::ID())
				{
					break;
				}
				current = ResourceAssets::GetAsset(parent);
			}
			return result;
		}


		struct DirectoryToScan
		{
			String path;
			RID    directory;
			String absolutePath;
		};
	}

	RID ResourceAssetHandler::Load(RID asset, StringView path)
	{
		String bufferPath = Path::Join(Path::Parent(path), Path::Name(path).Append(".buffer"));
		ByteBuffer buffer;

		if (FileHandler fileHandler = FileSystem::OpenFile(bufferPath, AccessMode::ReadOnly))
		{
			buffer.Resize(FileSystem::GetFileSize(fileHandler));
			FileSystem::ReadFile(fileHandler, buffer.begin(), buffer.Size());
			FileSystem::CloseFile(fileHandler);
		}

		YamlArchiveReader reader(FileSystem::ReadFileAsString(path), buffer);
		return Resources::Deserialize(reader);
	}

	void ResourceAssetHandler::Save(RID asset, StringView absolutePath)
	{
		YamlArchiveWriter writer;
		Resources::Serialize(asset, writer);
		FileSystem::SaveFileAsString(absolutePath, writer.EmitAsString());

		if (Span<u8> blobs = writer.GetBlobs(); !blobs.Empty())
		{
			String bufferPath = Path::Join(Path::Parent(absolutePath), Path::Name(absolutePath).Append(".buffer"));
			FileSystem::SaveFileAsByteArray(bufferPath, blobs);
		}
	}

	RID ResourceAssetHandler::Create(UUID uuid, UndoRedoScope* scope)
	{
		RID asset = Resources::Create(GetResourceTypeId(), UUID::RandomUUID(), scope);
		ResourceObject assetObject = Resources::Write(asset);
		assetObject.Commit(scope);
		return asset;
	}

	void ResourceAssetHandler::Reloaded(RID asset, StringView absolutePath)
	{
		//do nothing
	}

	RID ResourceAssets::ScanAssetsFromDirectory(StringView packageName, StringView packagePack)
	{
		std::optional<DirectoryToScan> currentScanItem;

		RID            package = Resources::Create<ResourceAssetPackage>();
		ResourceObject packageObject = Resources::Write(package);

		std::queue<DirectoryToScan> pendingItems;

		//package asset
		{
			String path = String(packageName).Append(":/");
			logger.Debug("Scanning package directory path '{}' absolutePath '{}'", path, packagePack);

			RID asset = Resources::Create<ResourceAsset>();
			RID assetFile = Resources::Create<ResourceAssetFile>();

			ResourceObject assetObject = Resources::Write(asset);
			assetObject.SetString(ResourceAsset::Name, packageName);
			assetObject.SetString(ResourceAsset::PathId, path);
			assetObject.SetString(ResourceAsset::Extension, "");
			assetObject.SetBool(ResourceAsset::Directory, true);
			assetObject.SetReference(ResourceAsset::AssetFile, assetFile);
			assetObject.Commit();

			RID            directory = Resources::Create<ResourceAssetDirectory>();
			ResourceObject directoryObject = Resources::Write(directory);
			directoryObject.SetSubObject(ResourceAssetDirectory::DirectoryAsset, asset);
			directoryObject.Commit();


			ResourceObject assetFileObject = Resources::Write(assetFile);
			assetFileObject.SetReference(ResourceAssetFile::AssetRef, asset);
			assetFileObject.SetString(ResourceAssetFile::AbsolutePath, packagePack);
			assetFileObject.SetString(ResourceAssetFile::RelativePath, path);
			assetFileObject.SetUInt(ResourceAssetFile::PersistedVersion, Resources::GetVersion(asset));
			assetFileObject.Commit();

			packageObject.AddToSubObjectList(ResourceAssetPackage::Files, assetFile);
			packageObject.SetSubObject(ResourceAssetPackage::Root, directory);

			loadedPackages.Insert(packageName, packagePack);

			currentScanItem = std::make_optional(DirectoryToScan{
				.path = path,
				.directory = directory,
				.absolutePath = packagePack
			});
		}

		while (currentScanItem.has_value())
		{
			DirectoryToScan& scan = currentScanItem.value();

			logger.Debug("Scanning directory {} ", scan.absolutePath);

			ResourceObject currentDirectory = Resources::Write(scan.directory);

			for (const String& entry : DirectoryEntries(scan.absolutePath))
			{
				String nameExtension = Path::ExtractName(scan.absolutePath, entry);
				if (nameExtension.Empty())
				{
					continue;
				}
				if (nameExtension[0] == '.') continue;

				String extension = Path::Extension(entry);
				if (extension == ".buffer") continue;

				String fileName = Path::Name(entry);
				String path = String().Append(scan.path).Append("/").Append(fileName).Append(extension);

				FileStatus status = FileSystem::GetFileStatus(entry);

				RID asset = Resources::Create<ResourceAsset>();
				RID assetFile = Resources::Create<ResourceAssetFile>();

				ResourceObject assetObject = Resources::Write(asset);
				assetObject.SetString(ResourceAsset::Name, fileName);
				assetObject.SetString(ResourceAsset::Extension, extension);
				assetObject.SetString(ResourceAsset::PathId, path);
				assetObject.SetReference(ResourceAsset::Parent, scan.directory);
				assetObject.SetBool(ResourceAsset::Directory, status.isDirectory);
				assetObject.SetReference(ResourceAsset::AssetFile, assetFile);

				if (!status.isDirectory)
				{
					RID object;
					if (auto it = handlersByExtension.Find(extension))
					{
						object = it->second->Load(asset, entry);
					}
					else
					{
						object = Resources::Create<ResourceFile>();
						ResourceObject resourceFileObject = Resources::Write(object);
						resourceFileObject.SetString(ResourceFile::Name, fileName);
						resourceFileObject.Commit();
					}

					if (object)
					{
						assetObject.SetSubObject(ResourceAsset::Object, object);
						Resources::SetPath(object, path);
					}
				}

				assetObject.Commit();

				ResourceObject assetFileObject = Resources::Write(assetFile);
				assetFileObject.SetReference(ResourceAssetFile::AssetRef, asset);
				assetFileObject.SetString(ResourceAssetFile::AbsolutePath, entry);
				assetFileObject.SetString(ResourceAssetFile::RelativePath, path);
				assetFileObject.SetUInt(ResourceAssetFile::PersistedVersion, Resources::GetVersion(asset));
				assetFileObject.SetUInt(ResourceAssetFile::TotalSizeInDisk, status.fileSize);
				assetFileObject.SetUInt(ResourceAssetFile::LastModifiedTime, status.lastModifiedTime);

				assetFileObject.Commit();

				packageObject.AddToSubObjectList(ResourceAssetPackage::Files, assetFile);

				if (status.isDirectory)
				{
					RID            directoryAsset = Resources::Create<ResourceAssetDirectory>();
					ResourceObject directoryObject = Resources::Write(directoryAsset);
					directoryObject.SetSubObject(ResourceAssetDirectory::DirectoryAsset, asset);
					directoryObject.Commit();

					currentDirectory.AddToSubObjectList(ResourceAssetDirectory::Directories, directoryAsset);

					pendingItems.emplace(DirectoryToScan{
						.path = path,
						.directory = directoryAsset,
						.absolutePath = entry
					});

					logger.Debug("directory '{}' loaded", path);
				}
				else
				{
					currentDirectory.AddToSubObjectList(ResourceAssetDirectory::Assets, asset);
					logger.Debug("asset '{}' registered", path);
				}
			}

			currentDirectory.Commit();

			currentScanItem.reset();
			if (!pendingItems.empty())
			{
				currentScanItem = std::make_optional(pendingItems.front());
				pendingItems.pop();
			}
		}

		packageObject.Commit();

		return package;
	}

	void ResourceAssets::SaveAssetsToDirectory(StringView directory, RID package, Span<UpdatedAssetInfo> items)
	{
		ResourceObject packageObject = Resources::Write(package);

		for (const UpdatedAssetInfo& assetToUpdate : items)
		{
			if (!assetToUpdate.shouldUpdate) continue;

			ResourceStorage* storage = Resources::GetStorage(assetToUpdate.asset);

			if (assetToUpdate.type == UpdatedAssetType::Updated)
			{
				String     absolutePath = Path::Join(directory, GetNewAbsolutePath(assetToUpdate.asset));
				StringView oldAbsolutePath = Resources::Read(assetToUpdate.assetFile).GetString(ResourceAssetFile::AbsolutePath);
				if (absolutePath != oldAbsolutePath)
				{
					// FileSystem::Rename(Path::Join(Path::Parent(oldAbsolutePath), Path::Name(oldAbsolutePath), ".buffers"),
					// 				   Path::Join(Path::Parent(absolutePath), Path::Name(absolutePath), ".buffers"));

					if (FileSystem::Rename(oldAbsolutePath, absolutePath))
					{
						logger.Debug("asset moved from {} to {} ", oldAbsolutePath, absolutePath);
					}
					else
					{
						logger.Error("failed to move asset from {} to {} ", oldAbsolutePath, absolutePath);
					}
				}

				ResourceObject assetObject = Resources::Read(assetToUpdate.asset);
				if (RID object = assetObject.GetSubObject(ResourceAsset::Object))
				{
					if (auto it = handlersByTypeID.Find(Resources::GetType(object)->GetID()))
					{
						it->second->Save(object, absolutePath);
					}
				}

				FileStatus status = FileSystem::GetFileStatus(absolutePath);

				ResourceObject assetFileObject = Resources::Write(assetToUpdate.assetFile);
				assetFileObject.SetString(ResourceAssetFile::AbsolutePath, absolutePath);
				assetFileObject.SetString(ResourceAssetFile::RelativePath, assetObject.GetString(ResourceAsset::PathId));
				assetFileObject.SetUInt(ResourceAssetFile::PersistedVersion, storage->version);
				assetFileObject.SetUInt(ResourceAssetFile::TotalSizeInDisk, status.fileSize);
				assetFileObject.SetUInt(ResourceAssetFile::LastModifiedTime, status.lastModifiedTime);
				assetFileObject.Commit();

				logger.Debug("asset '{}' saved on '{}' ", assetObject.GetString(ResourceAsset::PathId), absolutePath);
			}

			if (assetToUpdate.type == UpdatedAssetType::Created)
			{
				String absolutePath = Path::Join(directory, GetNewAbsolutePath(assetToUpdate.asset));
				String parentPath = Path::Parent(absolutePath);

				RID assetFile = Resources::Create<ResourceAssetFile>();

				{
					ResourceObject assetObject = Resources::Write(assetToUpdate.asset);
					assetObject.SetReference(ResourceAsset::AssetFile, assetFile);
					assetObject.Commit();
				}

				if (!FileSystem::GetFileStatus(parentPath).exists)
				{
					FileSystem::CreateDirectory(parentPath);
					logger.Debug("directory created on {} ", parentPath);
				}

				if (storage->parentFieldIndex == ResourceAssetDirectory::DirectoryAsset)
				{
					if (!FileSystem::GetFileStatus(absolutePath).exists)
					{
						FileSystem::CreateDirectory(absolutePath);
						logger.Debug("directory created on {} ", absolutePath);
					}
				}

				ResourceObject assetObject = Resources::Read(assetToUpdate.asset);
				if (RID object = assetObject.GetSubObject(ResourceAsset::Object))
				{
					if (auto it = handlersByTypeID.Find(Resources::GetType(object)->GetID()))
					{
						it->second->Save(object, absolutePath);
					}
				}

				FileStatus status = FileSystem::GetFileStatus(absolutePath);

				ResourceObject assetFileObject = Resources::Write(assetFile);
				assetFileObject.SetReference(ResourceAssetFile::AssetRef, assetToUpdate.asset);
				assetFileObject.SetString(ResourceAssetFile::AbsolutePath, absolutePath);
				assetFileObject.SetString(ResourceAssetFile::RelativePath, assetObject.GetString(ResourceAsset::PathId));
				assetFileObject.SetUInt(ResourceAssetFile::PersistedVersion, storage->version);
				assetFileObject.SetUInt(ResourceAssetFile::TotalSizeInDisk, status.fileSize);
				assetFileObject.SetUInt(ResourceAssetFile::LastModifiedTime, status.lastModifiedTime);
				assetFileObject.Commit();

				packageObject.AddToSubObjectList(ResourceAssetPackage::Files, assetFile);

				logger.Debug("asset {} created on {} ", assetObject.GetString(ResourceAsset::PathId), absolutePath);
			}

			if (assetToUpdate.type == UpdatedAssetType::Deleted)
			{
				ResourceObject assetFileObject = Resources::Read(assetToUpdate.assetFile);
				StringView     absolutePath = assetFileObject.GetString(ResourceAssetFile::AbsolutePath);
				FileSystem::Remove(absolutePath);
				FileSystem::Remove(Path::Join(Path::Parent(absolutePath), Path::Name(absolutePath).Append(".buffer")));
				logger.Debug("asset file removed from {} ", absolutePath);

				Resources::Destroy(assetToUpdate.assetFile);
			}
		}

		packageObject.Commit();
	}

	void ResourceAssets::GetUpdatedAssets(RID directory, Array<UpdatedAssetInfo>& items)
	{
		ResourceObject packageObject = Resources::Read(directory);
		Span<RID> files = packageObject.GetSubObjectList(ResourceAssetPackage::Files);
		for (RID assetFile : files)
		{
			ResourceObject assetFileObject = Resources::Read(assetFile);
			StringView     absolutePath = assetFileObject.GetString(ResourceAssetFile::AbsolutePath);
			RID            asset = assetFileObject.GetReference(ResourceAssetFile::AssetRef);
			ResourceObject assetObject = Resources::Read(asset);

			if (!assetObject)
			{
				items.EmplaceBack(UpdatedAssetInfo{
					.type = UpdatedAssetType::Deleted,
					.asset = asset,
					.assetFile = assetFile,
					.displayName = Path::Name(absolutePath) + Path::Extension(absolutePath),
					.path = assetFileObject.GetString(ResourceAssetFile::RelativePath),
					.shouldUpdate = true
				});
			}
			else if (assetObject.GetStorage()->version != assetFileObject.GetUInt(ResourceAssetFile::PersistedVersion))
			{
				items.EmplaceBack(UpdatedAssetInfo{
					.type = UpdatedAssetType::Updated,
					.asset = asset,
					.assetFile = assetFile,
					.displayName = String(assetObject.GetString(ResourceAsset::Name)).Append(assetObject.GetString(ResourceAsset::Extension)),
					.path = assetObject.GetString(ResourceAsset::PathId),
					.shouldUpdate = true
				});
			}
		}
		std::queue<RID> directoriesToScan;
		directoriesToScan.emplace(packageObject.GetSubObject(ResourceAssetPackage::Root));

		while (!directoriesToScan.empty())
		{
			RID rid = directoriesToScan.front();
			directoriesToScan.pop();

			ResourceObject directoryObject = Resources::Read(rid);

			auto checkAssetFile = [&](RID asset)
			{
				ResourceObject assetObject = Resources::Read(asset);
				if (!assetObject.HasValue(ResourceAsset::AssetFile) || !Resources::HasValue(assetObject.GetReference(ResourceAsset::AssetFile)))
				{
					items.EmplaceBack(UpdatedAssetInfo{
						.type = UpdatedAssetType::Created,
						.asset = asset,
						.assetFile = {},
						.displayName = String(assetObject.GetString(ResourceAsset::Name)).Append(assetObject.GetString(ResourceAsset::Extension)),
						.path = assetObject.GetString(ResourceAsset::PathId),
						.shouldUpdate = true
					});
				}
			};

			directoryObject.IterateSubObjectList(ResourceAssetDirectory::Assets, checkAssetFile);
			directoryObject.IterateSubObjectList(ResourceAssetDirectory::Directories, [&](RID rid)
			{
				checkAssetFile(GetAsset(rid));
				directoriesToScan.emplace(rid);
			});
		}
	}

	void ResourceAssets::OpenAsset(RID rid)
	{
		ResourceObject object = Resources::Read(rid);
		StringView     extension = object.GetString(ResourceAsset::Extension);

		if (auto it = handlersByExtension.Find(extension))
		{
			it->second->OpenAsset(rid);
		}
		else
		{
			SDL_OpenURL(GetAbsolutePath(rid).CStr());
		}
	}

	void ResourceAssets::ImportAsset(RID parent, StringView path)
	{
		if (!FileSystem::GetFileStatus(path).isDirectory)
		{
			pendingImports.EmplaceBack(parent, path);
			return;
		}

		for (const String& file: DirectoryEntries(path))
		{
			pendingImports.EmplaceBack(parent, file);
		}
	}

	RID ResourceAssets::CreateAsset(RID parent, TypeID typeId, StringView desiredName, UndoRedoScope* scope)
	{
		return CreateImportedAsset(parent, typeId, desiredName, scope, "");
	}

	RID ResourceAssets::DuplicateAsset(RID parent, RID sourceAsset, StringView desiredName, UndoRedoScope* scope)
	{
		if (ResourceAssetHandler* handler = GetAssetHandler(sourceAsset))
		{
			String newName = CreateUniqueAssetName(parent, desiredName.Empty() ? GetAssetName(sourceAsset) : String(desiredName), false);
			String path = GetDirectoryPathId(parent) + "/" + newName + handler->Extension();

			RID asset = Resources::Clone(sourceAsset, UUID::RandomUUID(), scope);

			RID rid = Resources::Create<ResourceAsset>(UUID::RandomUUID(), scope);

			ResourceObject object = Resources::Write(rid);
			object.SetString(ResourceAsset::Name, newName);
			object.SetString(ResourceAsset::Extension, handler->Extension());
			object.SetSubObject(ResourceAsset::Object, asset);
			object.SetReference(ResourceAsset::Parent, parent);
			object.SetString(ResourceAsset::PathId, path);
			object.SetBool(ResourceAsset::Directory, false);

			object.Commit(scope);

			ResourceObject parentObject = Resources::Write(parent);
			parentObject.AddToSubObjectList(ResourceAssetDirectory::Assets, rid);
			parentObject.Commit(scope);

			logger.Debug("asset from type {} created with uuid {} name {} ", handler->GetDesc(), Resources::GetUUID(asset).ToString(), newName);

			return asset;
		}

		return {};
	}

	RID ResourceAssets::CreateInheritedAsset(RID parent, RID sourceAsset, StringView desiredName, UndoRedoScope* scope)
	{
		if (ResourceAssetHandler* handler = GetAssetHandler(sourceAsset))
		{
			String newName = CreateUniqueAssetName(parent, desiredName.Empty() ? GetAssetName(sourceAsset).Append(" (Inherited)") : String(desiredName), false);
			String path = GetDirectoryPathId(parent) + "/" + newName + handler->Extension();

			RID asset = Resources::CreateFromPrototype(sourceAsset, UUID::RandomUUID(), scope);

			RID rid = Resources::Create<ResourceAsset>(UUID::RandomUUID(), scope);

			ResourceObject object = Resources::Write(rid);
			object.SetString(ResourceAsset::Name, newName);
			object.SetString(ResourceAsset::Extension, handler->Extension());
			object.SetSubObject(ResourceAsset::Object, asset);
			object.SetReference(ResourceAsset::Parent, parent);
			object.SetString(ResourceAsset::PathId, path);
			object.SetBool(ResourceAsset::Directory, false);

			object.Commit(scope);

			ResourceObject parentObject = Resources::Write(parent);
			parentObject.AddToSubObjectList(ResourceAssetDirectory::Assets, rid);
			parentObject.Commit(scope);

			logger.Debug("asset from type {} created with uuid {} name {} ", handler->GetDesc(), Resources::GetUUID(asset).ToString(), newName);

			return asset;
		}

		return {};
	}

	RID ResourceAssets::CreateImportedAsset(RID parent, TypeID typeId, StringView desiredName, UndoRedoScope* scope, StringView sourcePath)
	{
		if (auto it = handlersByTypeID.Find(typeId))
		{
			ResourceAssetHandler* handler = it->second;

			String newName = CreateUniqueAssetName(parent, desiredName.Empty() ? String("New ").Append(handler->GetDesc()) : String(desiredName), false);
			String path = GetDirectoryPathId(parent) + "/" + newName + handler->Extension();

			RID asset = it->second->Create(UUID::RandomUUID(), scope);

			RID rid = Resources::Create<ResourceAsset>(UUID::RandomUUID(), scope);

			ResourceObject object = Resources::Write(rid);
			object.SetString(ResourceAsset::Name, newName);
			object.SetString(ResourceAsset::Extension, handler->Extension());
			object.SetSubObject(ResourceAsset::Object, asset);
			object.SetReference(ResourceAsset::Parent, parent);
			object.SetString(ResourceAsset::PathId, path);
			object.SetBool(ResourceAsset::Directory, false);

			if (!sourcePath.Empty())
			{
				object.SetString(ResourceAsset::SourcePath, sourcePath);
			}

			object.Commit(scope);

			ResourceObject parentObject = Resources::Write(parent);
			parentObject.AddToSubObjectList(ResourceAssetDirectory::Assets, rid);
			parentObject.Commit(scope);

			logger.Debug("asset from type {} created with uuid {} name {} ", handler->GetDesc(), Resources::GetUUID(asset).ToString(), newName);

			return asset;
		}

		logger.Error("asset from type {} cannot be created, no handler found for it", typeId);
		return {};
	}

	RID ResourceAssets::FindAssetOnDirectory(RID directory, TypeID typeId, StringView name)
	{
		if (!directory) return {};

		String fullName = name;

		if (ResourceAssetHandler* handler = GetAssetHandler(typeId))
		{
			fullName += handler->Extension();
		}

		ResourceObject parentObject = Resources::Read(directory);

		for (RID child :  parentObject.GetSubObjectList(ResourceAssetDirectory::Assets))
		{
			if (fullName == GetAssetFullName(child))
			{
				ResourceObject resourceObject = Resources::Read(child);
				return resourceObject.GetSubObject(ResourceAsset::Object);
			}
		}

		return {};
	}

	RID ResourceAssets::CreateDirectory(RID parent, StringView desiredName, UndoRedoScope* scope)
	{
		String newName = CreateUniqueAssetName(parent, desiredName, true);
		String path = GetDirectoryPathId(parent) + "/" + newName;

		RID            asset = Resources::Create<ResourceAsset>();
		ResourceObject object = Resources::Write(asset);
		object.SetString(ResourceAsset::Name, newName);
		object.SetString(ResourceAsset::Extension, "");
		object.SetReference(ResourceAsset::Parent, parent);
		object.SetString(ResourceAsset::PathId, path);
		object.SetBool(ResourceAsset::Directory, true);
		object.Commit(scope);

		RID            directory = Resources::Create<ResourceAssetDirectory>();
		ResourceObject directoryObject = Resources::Write(directory);
		directoryObject.SetSubObject(ResourceAssetDirectory::DirectoryAsset, asset);
		directoryObject.Commit(scope);

		ResourceObject parentObject = Resources::Write(parent);
		parentObject.AddToSubObjectList(ResourceAssetDirectory::Directories, directory);
		parentObject.Commit(scope);

		return asset;
	}

	String ResourceAssets::CreateUniqueAssetName(RID parent, StringView desiredName, bool directory)
	{
		if (!parent) return {};

		ResourceObject parentObject = Resources::Read(parent);
		Array<RID>     children = parentObject.GetSubObjectList(directory ? ResourceAssetDirectory::Directories : ResourceAssetDirectory::Assets);

		u32    count{};
		String finalName = desiredName;
		bool   nameFound;
		do
		{
			nameFound = true;

			for (RID rid : children)
			{
				if (directory)
				{
					ResourceObject directoryObject = Resources::Read(rid);
					rid = directoryObject.GetSubObject(ResourceAssetDirectory::DirectoryAsset);
				}

				ResourceObject childObject = Resources::Read(rid);

				if (childObject && finalName == childObject.GetString(ResourceAsset::Name))
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

	void ResourceAssets::MoveAsset(RID newParent, RID rid, UndoRedoScope* scope)
	{
		//get directory asset instead of asset
		newParent = Resources::GetParent(newParent);
		if (!newParent) return;

		RID oldParent = Resources::GetParent(rid);
		if (oldParent == newParent) return;

		ResourceObject assetObject = Resources::Read(rid);
		bool isDirectory = assetObject.GetBool(ResourceAsset::Directory);
		String newName = CreateUniqueAssetName(newParent, assetObject.GetString(ResourceAsset::Name), isDirectory);

		if (!isDirectory)
		{
			ResourceObject oldParentObject = Resources::Write(oldParent);
			oldParentObject.RemoveFromSubObjectList(ResourceAssetDirectory::Assets, rid);
			oldParentObject.Commit(scope);

			ResourceObject newParentObject = Resources::Write(newParent);
			newParentObject.AddToSubObjectList(ResourceAssetDirectory::Assets, rid);
			newParentObject.Commit(scope);
		}
		else
		{
			RID dirAsset = Resources::GetParent(rid);
			oldParent = Resources::GetParent(oldParent);

			ResourceObject oldParentObject = Resources::Write(oldParent);
			oldParentObject.RemoveFromSubObjectList(ResourceAssetDirectory::Directories, dirAsset);
			oldParentObject.Commit(scope);

			ResourceObject newParentObject = Resources::Write(newParent);
			newParentObject.AddToSubObjectList(ResourceAssetDirectory::Directories, dirAsset);
			newParentObject.Commit(scope);
		}

		ResourceObject write = Resources::Write(rid);
		write.SetString(ResourceAsset::Name, newName);
		write.Commit(scope);

		//TODO PathID?
	}

	String ResourceAssets::GetDirectoryPathId(RID directory)
	{
		ResourceObject directoryObject = Resources::Read(directory);
		RID            directoryAsset = directoryObject.GetSubObject(ResourceAssetDirectory::DirectoryAsset);
		ResourceObject assetObject = Resources::Read(directoryAsset);
		return assetObject.GetString(ResourceAsset::PathId);
	}

	StringView ResourceAssets::GetAbsolutePath(RID asset)
	{
		RID resourceAsset = asset;
		if (Resources::GetStorage(asset)->resourceType->GetID() != TypeInfo<ResourceAsset>::ID())
		{
			resourceAsset = Resources::GetParent(asset);
		}

		ResourceObject assetObject = Resources::Read(resourceAsset);
		if (RID assetFile = assetObject.GetReference(ResourceAsset::AssetFile))
		{
			ResourceObject assetFileObject = Resources::Read(assetFile);
			return assetFileObject.GetString(ResourceAssetFile::AbsolutePath);
		}
		return "";
	}

	StringView ResourceAssets::GetPathId(RID asset)
	{
		if (ResourceObject assetObject = Resources::Read(asset))
		{
			return assetObject.GetString(ResourceAsset::PathId);
		}
		return {};
	}

	RID ResourceAssets::GetAsset(RID rid)
	{
		if (Resources::GetStorage(rid)->resourceType->GetID() == TypeInfo<ResourceAssetDirectory>::ID())
		{
			ResourceObject directoryObject = Resources::Read(rid);
			return directoryObject.GetSubObject(ResourceAssetDirectory::DirectoryAsset);
		}

		if (Resources::GetStorage(rid)->resourceType->GetID() == TypeInfo<ResourceAsset>::ID())
		{
			ResourceObject assetObject = Resources::Read(rid);
			return assetObject.GetSubObject(ResourceAsset::Object);
		}

		return rid;
	}

	String ResourceAssets::GetAbsolutePathFromPathId(StringView pathId)
	{
		if (pathId.Empty()) return {};

		StringView paths[2];
		u32 i = 0;
		Split(pathId, StringView{":/"}, [&](StringView path)
		{
			if (i>2) return;
			paths[i++] = path;
		});

		if (auto it = loadedPackages.Find(paths[0]))
		{
			return Path::Join(it->second, paths[1]);
		}

		return "";
	}

	RID ResourceAssets::GetParentAsset(RID rid)
	{
		ResourceObject directoryObject = Resources::Read(rid);
		if (directoryObject.GetBool(ResourceAsset::Directory))
		{
			return Resources::GetParent(Resources::GetParent(rid));
		}
		return Resources::GetParent(rid);
	}

	bool ResourceAssets::IsChildOf(RID parent, RID child)
	{
		return Resources::Read(parent).HasOnSubObjectList(ResourceAssetDirectory::Directories, child);
	}

	bool ResourceAssets::IsUpdated(RID rid)
	{
		u64 currentVersion;
		u64 persistedVersion;

		if (GetAssetVersions(rid, currentVersion, persistedVersion))
		{
			return currentVersion != persistedVersion;
		}

		return true;
	}

	bool ResourceAssets::GetAssetVersions(RID rid, u64& currentVersion, u64& persistedVersion)
	{
		ResourceObject assetObject = Resources::Read(rid);
		if (!assetObject)
		{
			return false;
		}

		RID assetFile = assetObject.GetReference(ResourceAsset::AssetFile);
		if (!assetFile)
		{
			return false;
		}

		ResourceObject assetFileObject = Resources::Read(assetFile);
		if (!assetFileObject)
		{
			return false;
		}

		currentVersion = assetObject.GetVersion();
		persistedVersion = assetFileObject.GetUInt(ResourceAssetFile::PersistedVersion);

		return true;
	}

	ResourceAssetHandler* ResourceAssets::GetAssetHandler(RID rid)
	{
		ResourceType* resource = Resources::GetStorage(rid)->resourceType;
		if (!resource) return nullptr;

		TypeID typeId = resource->GetID();
		if (typeId == TypeInfo<ResourceAsset>::ID())
		{
			if (ResourceObject assetObject = Resources::Read(rid))
			{
				if (String extension = assetObject.GetString(ResourceAsset::Extension); !extension.Empty())
				{
					if (auto it = handlersByExtension.Find(extension))
					{
						return it->second;
					}
				}
			}
		}

		return GetAssetHandler(typeId);
	}

	ResourceAssetHandler* ResourceAssets::GetAssetHandler(TypeID typeId)
	{
		if (auto it = handlersByTypeID.Find(typeId))
		{
			return it->second;
		}
		return nullptr;
	}

	String ResourceAssets::GetAssetName(RID rid)
	{
		if (!rid) return {};

		ResourceType* type = Resources::GetType(rid);
		if (type && type->GetID() == TypeInfo<ResourceAssetDirectory>::ID())
		{
			if (ResourceObject obj = Resources::Read(rid))
			{
				rid = obj.GetSubObject(ResourceAssetDirectory::DirectoryAsset);
			}
		}

		if (ResourceAssetHandler* assetHandler = GetAssetHandler(rid))
		{
			String name;
			if (assetHandler->GetAssetName(rid, name))
			{
				return name;
			}
		}

		if (RID parent = Resources::GetParent(rid);
			Resources::GetStorage(parent)->resourceType != nullptr &&
			Resources::GetStorage(parent)->resourceType->GetID() == TypeInfo<ResourceAsset>::ID() &&
			Resources::HasValue(parent))
		{
			ResourceObject obj = Resources::Read(parent);

			String name = obj.GetString(ResourceAsset::Name);
			name += obj.GetString(ResourceAsset::Extension);
			return name;
		}

		//not sure?
		if (ResourceObject obj = Resources::Read(rid))
		{
			return obj.GetString(0);
		}

		return {};
	}

	String ResourceAssets::GetAssetFullName(RID rid)
	{
		if (!rid) return {};

		String name = GetAssetName(rid);

		if (ResourceAssetHandler* handler = GetAssetHandler(rid))
		{
			name += handler->Extension();
		}
		return name;
	}

	UUID ResourceAssets::GetAssetUUID(RID rid)
	{
		if (Resources::GetStorage(rid)->resourceType->GetID() == TypeInfo<ResourceAsset>::ID())
		{
			if (ResourceObject obj = Resources::Read(rid))
			{
				return Resources::GetUUID(obj.GetSubObject(ResourceAsset::Object));
			}
		}
		return Resources::GetUUID(rid);
	}

	void ResourceAssets::WatchAsset(RID asset, StringView absolutePath)
	{
		fileWatcher.Watch(IntToPtr(asset.id), absolutePath);
	}

	void ResourceAssets::ExportPackages(Span<RID> packages, ArchiveWriter& writer)
	{
		for (RID package: packages)
		{
			ResourceObject packageObject = Resources::Read(package);

			std::queue<RID> directoriesToScan;
			directoriesToScan.emplace(packageObject.GetSubObject(ResourceAssetPackage::Root));

			while (!directoriesToScan.empty())
			{
				RID rid = directoriesToScan.front();
				directoriesToScan.pop();
				ResourceObject directoryObject = Resources::Read(rid);

				auto checkAssetFile = [&](RID asset)
				{
					ResourceObject assetObject = Resources::Read(asset);
					if (RID object = assetObject.GetSubObject(ResourceAsset::Object))
					{
						writer.BeginMap();
						{
							writer.WriteString("pathId", GetPathId(asset));
							Resources::Serialize(object, writer);
						}
						writer.EndMap();
					}
				};

				directoryObject.IterateSubObjectList(ResourceAssetDirectory::Assets, checkAssetFile);
				directoryObject.IterateSubObjectList(ResourceAssetDirectory::Directories, [&](RID rid)
				{
					directoriesToScan.emplace(rid);
				});
			}
		}
	}

	void ResourceAssetsUpdate()
	{
		//TODO : multithreading with task system :)
		if (!pendingImports.Empty())
		{
			UndoRedoScope* scope = nullptr;

			for (const AssetsPendingImport& toImport : pendingImports)
			{
				logger.Debug("importing {} to {} ", toImport.path, ResourceAssets::GetDirectoryPathId(toImport.parent));
				String extension = Path::Extension(toImport.path);
				extension = extension.ToLowerCase();

				if (auto it = importersByExtension.Find(extension))
				{
					if (scope == nullptr)
					{
						scope = Editor::CreateUndoRedoScope("Import Assets");
					}

					it->second->ImportAsset(toImport.parent, nullptr, toImport.path, scope);
				}
			}
			pendingImports.Clear();
		}


		fileWatcher.CheckForUpdates([](const FileWatcherModified& modified)
		{
			RID asset = RID(PtrToInt(modified.userData));
			if (ResourceAssetHandler* handler = ResourceAssets::GetAssetHandler(asset))
			{
				if (modified.event == FileNotifyEvent::Modified)
				{
					handler->Reloaded(asset, modified.path);
				}
			}
		});
	}

	void ResourceAssetsShutdown()
	{
		for (ResourceAssetHandler* handler : handlers)
		{
			DestroyAndFree(handler);
		}

		for (ResourceAssetImporter* importer : importers)
		{
			DestroyAndFree(importer);
		}

		importersByExtension.Clear();
		importers.Clear();

		fileWatcher.Stop();
	}


	void ResourceAssetsInit()
	{
		Event::Bind<OnShutdown, ResourceAssetsShutdown>();
		Event::Bind<OnUpdate, ResourceAssetsUpdate>();

		for (TypeID derivedId : Reflection::GetDerivedTypes(TypeInfo<ResourceAssetHandler>::ID()))
		{
			if (ReflectType* type = Reflection::FindTypeById(derivedId))
			{
				if (Object* newObject = type->NewObject())
				{
					if (ResourceAssetHandler* handler = newObject->SafeCast<ResourceAssetHandler>())
					{
						logger.Debug("Registered asset handler {} for extension {} ", type->GetName(), handler->Extension());

						if (StringView extension = handler->Extension(); !extension.Empty())
						{
							handlersByExtension.Insert(extension, handler);
						}

						if (TypeID typeId = handler->GetResourceTypeId(); typeId != 0)
						{
							handlersByTypeID.Insert(typeId, handler);
						}
					}
				}
			}
		}

		for (TypeID derivedId : Reflection::GetDerivedTypes(TypeInfo<ResourceAssetImporter>::ID()))
		{
			if (ReflectType* type = Reflection::FindTypeById(derivedId))
			{
				if (ResourceAssetImporter* importer = type->NewObject()->SafeCast<ResourceAssetImporter>())
				{
					for (const String& extension : importer->ImportedExtensions())
					{
						logger.Debug("Registered asset importer {} for extension {} ", type->GetName(), extension);
						importersByExtension.Insert(extension, importer);
					}
				}
			}
		}

		fileWatcher.Start();
	}

	void OnUpdateAsset(ResourceObject& oldValue, ResourceObject& newValue, VoidPtr userData)
	{
		if (oldValue && newValue)
		{
			StringView oldName = oldValue.GetString(ResourceAsset::Name);
			StringView newName = newValue.GetString(ResourceAsset::Name);

			if (!oldName.Empty() && oldName != newName)
			{
				String parentPath = ResourceAssets::GetDirectoryPathId(newValue.GetReference(ResourceAsset::Parent));
				newValue.SetString(ResourceAsset::PathId, parentPath + "/" + newName + newValue.GetString(ResourceAsset::Extension));
			}


			StringView oldPath = oldValue.GetString(ResourceAsset::PathId);
			StringView newPath = newValue.GetString(ResourceAsset::PathId);

			if (oldPath != newPath)
			{
				logger.Debug("asset path updated from {} to {} ", oldPath, newPath);

				if (RID object = newValue.GetReference(ResourceAsset::Object))
				{
					Resources::SetPath(object, newPath);
				}

				// if (newValue.GetBool(ResourceAsset::Directory))
				// {
				// 	RID directoryRID = Resources::GetParent(newValue.GetRID());
				// 	if (Resources::GetType(directoryRID)->GetID() == TypeInfo<ResourceAssetDirectory>::ID())
				// 	{
				// 		if (ResourceObject directoryObject = Resources::Read(directoryRID))
				// 		{
				// 			auto fnUpdate = [&](RID asset)
				// 			{	// 				if (ResourceObject childObject = Resources::Write(asset))
				// 				{
				// 					childObject.SetString(ResourceAsset::PathId, String(newPath) + "/" + childObject.GetString(ResourceAsset::Name) + childObject.GetString(ResourceAsset::Extension));
				// 					childObject.Commit();
				// 				}
				// 			};
				//
				// 			for (RID directory : directoryObject.GetSubObjectList(ResourceAssetDirectory::Directories))
				// 			{
				// 				ResourceObject directoryObject = Resources::Read(directory);
				// 				RID asset = directoryObject.GetSubObject(ResourceAssetDirectory::DirectoryAsset);
				// 				fnUpdate(asset);
				// 			}
				//
				// 			for (RID asset : directoryObject.GetSubObjectList(ResourceAssetDirectory::Assets))
				// 			{
				// 				fnUpdate(asset);
				// 			}
				// 		}
				// 	}
				// }
			}
		}
	}

	void RegisterEntityHandler();
	void RegisterMaterialHandler();
	void RegisterTextureHandler();
	void RegisterMeshHandler();
	void RegisterDCCAssetHandler();
	void RegisterShaderHandler();

	void RegisterTextureImporter();
	void RegisterFBXImporter();
	void RegisterObjImporter();

	void RegisterResourceAssetTypes()
	{
		Resources::Type<ResourceAssetPackage>()
			.Field<ResourceAssetPackage::Name>(ResourceFieldType::String)
			.Field<ResourceAssetPackage::AbsolutePath>(ResourceFieldType::String)
			.Field<ResourceAssetPackage::Files>(ResourceFieldType::SubObjectList)
			.Field<ResourceAssetPackage::Root>(ResourceFieldType::SubObject)
			.Build();

		Resources::Type<ResourceAssetFile>()
			.Field<ResourceAssetFile::AssetRef>(ResourceFieldType::Reference)
			.Field<ResourceAssetFile::AbsolutePath>(ResourceFieldType::String)
			.Field<ResourceAssetFile::RelativePath>(ResourceFieldType::String)
			.Field<ResourceAssetFile::PersistedVersion>(ResourceFieldType::UInt)
			.Field<ResourceAssetFile::TotalSizeInDisk>(ResourceFieldType::UInt)
			.Field<ResourceAssetFile::LastModifiedTime>(ResourceFieldType::UInt)
			.Build();

		Resources::Type<ResourceAssetDirectory>()
			.Field<ResourceAssetDirectory::DirectoryAsset>(ResourceFieldType::SubObject)
			.Field<ResourceAssetDirectory::Directories>(ResourceFieldType::SubObjectList)
			.Field<ResourceAssetDirectory::Assets>(ResourceFieldType::SubObjectList)
			.Build();

		Resources::Type<ResourceAsset>()
			.Field<ResourceAsset::Name>(ResourceFieldType::String)
			.Field<ResourceAsset::Type>(ResourceFieldType::None)
			.Field<ResourceAsset::Extension>(ResourceFieldType::String)
			.Field<ResourceAsset::Object>(ResourceFieldType::SubObject)
			.Field<ResourceAsset::Parent>(ResourceFieldType::Reference)
			.Field<ResourceAsset::PathId>(ResourceFieldType::String)
			.Field<ResourceAsset::Directory>(ResourceFieldType::Bool)
			.Field<ResourceAsset::AssetFile>(ResourceFieldType::Reference)
			.Field<ResourceAsset::SourcePath>(ResourceFieldType::String)
			.Build();

		Resources::FindType<ResourceAsset>()->RegisterEvent(OnUpdateAsset, nullptr);

		RegisterEntityHandler();
		RegisterTextureHandler();
		RegisterMaterialHandler();
		RegisterMeshHandler();
		RegisterDCCAssetHandler();
		RegisterShaderHandler();

		RegisterTextureImporter();
		RegisterFBXImporter();
		RegisterObjImporter();
	}
}
