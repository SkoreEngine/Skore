#include "Skore/Resource/ResourceAssets.hpp"

#include <algorithm>
#include <mutex>
#include <optional>
#include <queue>

#include "Skore/App.hpp"
#include "Skore/Editor.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/Events.hpp"
#include "Skore/Core/ByteBuffer.hpp"
#include "Skore/Core/Event.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/Core/Settings.hpp"
#include "Skore/Core/StringUtils.hpp"
#include "Skore/Graphics/Device.hpp"
#include "Skore/Graphics/Graphics.hpp"
#include "Skore/ImGui/Icons.h"
#include "Skore/IO/Compression.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/FileTypes.hpp"
#include <efsw/efsw.hpp>
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/Resources.hpp"
#include "Skore/Resource/ResourceType.hpp"
#include "Skore/Utils/StaticContent.hpp"

namespace Skore
{
	namespace
	{
		struct AssetsPendingImport
		{
			RID    parent;
			String path;
		};

		struct ThumbnailData
		{
			GPUTexture* texture;
			u64         version;
			f64         lastCheck;
			bool        forceUpdate;
			bool        canUpdate;
		};


		HashMap<String, String> loadedPackages;

		HashMap<String, ResourceAssetHandler*> handlersByExtension;
		HashMap<TypeID, ResourceAssetHandler*> handlersByTypeID;

		HashMap<String, ResourceAssetImporter*> importersByExtension;

		HashMap<TypeID, ResourceAssetHandler*>  handlersByHandlerType;
		HashMap<TypeID, ResourceAssetImporter*> importersByImporterType;

		Array<AssetsPendingImport> pendingImports;

		struct FileWatchEvent
		{
			efsw::Action                          action;
			std::chrono::steady_clock::time_point timestamp;
		};

		constexpr auto fileWatchDebounceTime = std::chrono::milliseconds(200);

		class AssetFileListener : public efsw::FileWatchListener
		{
		public:
			void handleFileAction(efsw::WatchID watchid, const std::string& dir, const std::string& filename, efsw::Action action, std::string oldFilename) override
			{
				String fullPath = String(dir.c_str()).Append(filename.c_str());
				for (usize i = 0; i < fullPath.Size(); ++i)
				{
					if (fullPath[i] == '\\') fullPath[i] = '/';
				}
				std::unique_lock lock(mutex);
				pendingEvents[fullPath] = FileWatchEvent{.action = action, .timestamp = std::chrono::steady_clock::now()};
			}

			std::mutex                      mutex;
			HashMap<String, FileWatchEvent> pendingEvents;
		};

		AssetFileListener  fileListener;
		efsw::FileWatcher* efswWatcher = nullptr;

		GPUTexture* assertTexture = nullptr;

		f64                         currentTime = 0;
		String                      thumbnailDirectory;
		HashMap<RID, ThumbnailData> thumbnails;
		std::mutex                  thumbnailsMutex{};

		std::mutex                    assetsByTypeMutex{};
		HashMap<TypeID, HashSet<RID>> assetsByType;

		String bufferTempFolder;

		Logger& logger = Logger::GetLogger("Skore::ResourceAssets");


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
				current = ResourceAssets::GetAssetPayload(parent);
			}
			return result;
		}

		String MakeAssetBufferPath(StringView absolutePath)
		{
			return Path::Join(Path::Parent(absolutePath), Path::Name(absolutePath).Append(Path::Extension(absolutePath)).Append(".buffers"));
		}

		bool HasThumbnail(RID asset)
		{
			String thumbnailPath = Path::Join(thumbnailDirectory, ResourceAssets::GetAssetUUID(asset).ToString());
			return FileSystem::GetFileStatus(thumbnailPath).exists;
		}

		void UpdateThumbnailData(RID rid)
		{
			String thumbnailPath = Path::Join(thumbnailDirectory, ResourceAssets::GetAssetUUID(rid).ToString());
			if (!FileSystem::GetFileStatus(thumbnailPath).exists)
			{
				return;
			}

			GPUTexture* texture = nullptr;
			{
				std::unique_lock lock(thumbnailsMutex);
				auto             it = thumbnails.Find(rid);
				if (it == thumbnails.end() || it->second.texture == nullptr)
				{
					texture = Graphics::CreateTexture(TextureDesc{
						.extent = {thumbnailSize.width, thumbnailSize.height, 1},
						.format = TextureFormat::R8G8B8A8_UNORM,
						.usage = ResourceUsage::ShaderResource | ResourceUsage::CopyDest,
					});
				}
				else
				{
					texture = it->second.texture;
				}
			}

			Array<u8> compressedData;
			FileSystem::ReadFileAsByteArray(thumbnailPath, compressedData);

			if (!compressedData.Empty())
			{
				usize     bufferSize = Compression::GetMaxDecompressedBufferSize(compressedData.Data(), compressedData.Size(), CompressionMode::ZSTD);
				Array<u8> uncompressedData;
				uncompressedData.Resize(bufferSize);
				bufferSize = Compression::Decompress(uncompressedData.Data(), uncompressedData.Size(), compressedData.Data(), compressedData.Size(), CompressionMode::ZSTD);

				Graphics::UploadTextureData(TextureDataInfo{
					.texture = texture,
					.data = uncompressedData.Data(),
					.size = bufferSize,
				});

				std::unique_lock lock(thumbnailsMutex);
				thumbnails[rid].texture = texture;
				thumbnails[rid].version = ResourceAssets::GetAssetObjectVersion(rid);
			}
		}

		struct DirectoryToScan
		{
			String path;
			RID    directory;
			String absolutePath;
		};

		struct PendingAssetLoad
		{
			RID    directory;
			String path;
			String absolutePath;
			String extension;
			String fileName;
			u64    fileSize;
			u64    lastModifiedTime;
			int    loadOrder;
		};
	}

	RID ResourceAssetHandler::Load(RID asset, StringView path)
	{
		String     bufferPath = Path::Join(Path::Parent(path), Path::Name(path).Append(".buffer"));
		ByteBuffer buffer;

		if (FileHandler fileHandler = FileSystem::OpenFile(bufferPath, AccessMode::ReadOnly))
		{
			buffer.Resize(FileSystem::GetFileSize(fileHandler));
			FileSystem::ReadFile(fileHandler, buffer.begin(), buffer.Size());
			FileSystem::CloseFile(fileHandler);
		}

		YamlArchiveReader reader(FileSystem::ReadFileAsString(path), buffer);
		String bufferDirectoryPath = MakeAssetBufferPath(path);
		RID    object = Resources::Deserialize(reader);
		if (ResourceObject resourceObject = Resources::Read(object))
		{
			resourceObject.IterateAllBuffers([&](const ResourceBuffer& bufferObject)
			{
				String bufferFileName = Path::Join(bufferDirectoryPath, bufferObject.GetIdAsString() + ".buffer");
				bufferObject.MapFile(bufferFileName, true, 0, 0);
			});
		}

		return object;
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

		HashSet<String> currentBufferFiles;
		String          bufferPath = MakeAssetBufferPath(absolutePath);

		if (ResourceObject resourceObject = Resources::Read(asset))
		{
			resourceObject.IterateAllBuffers([&](const ResourceBuffer& buffer)
			{
				String bufferFileName = buffer.GetIdAsString() + ".buffer";

				String bufferAssetTempPath = Path::Join(bufferTempFolder, bufferFileName);
				String bufferAssetFilePath = Path::Join(bufferPath, bufferFileName);

				if (FileSystem::Rename(bufferAssetTempPath, bufferAssetFilePath))
				{
					logger.Debug("buffer '{}' saved on '{}' ", bufferFileName, bufferAssetFilePath);
					buffer.MapFile(bufferAssetFilePath, true, 0, 0);
				}
				currentBufferFiles.Emplace(bufferFileName);
			});
		}

		for (String& file : DirectoryEntries(bufferPath))
		{
			String bufferFileName = Path::Name(file) + Path::Extension(file);
			if (!currentBufferFiles.Has(bufferFileName))
			{
				String bufferAssetTempPath = Path::Join(bufferTempFolder, bufferFileName);
				if (FileSystem::Rename(file, bufferAssetTempPath))
				{
					logger.Debug("buffer file '{}' moved to temp folder", file);
				}
			}
		}
	}

	RID ResourceAssetHandler::Create(UUID uuid, UndoRedoScope* scope)
	{
		RID            asset = Resources::Create(GetResourceTypeId(), UUID::RandomUUID(), scope);
		ResourceObject assetObject = Resources::Write(asset);
		assetObject.Commit(scope);
		return asset;
	}

	void ResourceAssetHandler::Reloaded(RID asset, StringView absolutePath)
	{
		//do nothing
	}

	void ResourceAssetHandler::AfterMove(RID asset, StringView oldAbsolutePath, StringView newAbsolutePath) {}

	void ResourceAssetHandler::Export(RID object, ArchiveWriter& writer)
	{
		Resources::Serialize(object, writer);
	}


	FnThumbnailGenerator ResourceAssetHandler::GetThumbnailGenerator(RID rid) const
	{
		return nullptr;
	}

	const char* ResourceAssetHandler::GetIcon() const
	{
		return nullptr;
	}

	RID ResourceAssets::ScanPackageFromDirectory(StringView packageName, StringView packagePath)
	{
		std::optional<DirectoryToScan> currentScanItem;

		RID            package = Resources::Create<ResourceAssetPackage>();
		ResourceObject packageObject = Resources::Write(package);

		RID                         rootDirectoryRID;
		std::queue<DirectoryToScan> pendingItems;

		{
			String rootDirectory = Path::Join(packagePath, "Assets");
			String path = String(packageName).Append(":/");

			logger.Debug("Scanning package directory path '{}' absolutePath '{}'", path, rootDirectory);

			RID asset = Resources::Create<ResourceAsset>();
			RID assetFile = Resources::Create<ResourceAssetFile>();

			ResourceObject assetObject = Resources::Write(asset);
			assetObject.SetString(ResourceAsset::Name, packageName);
			assetObject.SetString(ResourceAsset::PathId, path);
			assetObject.SetString(ResourceAsset::Extension, "");
			assetObject.SetBool(ResourceAsset::Directory, true);
			assetObject.SetReference(ResourceAsset::AssetFile, assetFile);
			assetObject.Commit();

			rootDirectoryRID = Resources::Create<ResourceAssetDirectory>();
			ResourceObject directoryObject = Resources::Write(rootDirectoryRID);
			directoryObject.SetSubObject(ResourceAssetDirectory::DirectoryAsset, asset);
			directoryObject.Commit();

			ResourceObject assetFileObject = Resources::Write(assetFile);
			assetFileObject.SetReference(ResourceAssetFile::AssetRef, asset);
			assetFileObject.SetString(ResourceAssetFile::AbsolutePath, rootDirectory);
			assetFileObject.SetString(ResourceAssetFile::RelativePath, path);
			assetFileObject.SetUInt(ResourceAssetFile::PersistedVersion, Resources::GetVersion(asset));
			assetFileObject.Commit();

			packageObject.AddToSubObjectList(ResourceAssetPackage::Files, assetFile);

			loadedPackages.Insert(packageName, rootDirectory);

			if (efswWatcher)
			{
				std::string dir(rootDirectory.CStr(), rootDirectory.Size());
				efswWatcher->addWatch(dir, &fileListener, true);
			}

			currentScanItem = std::make_optional(DirectoryToScan{
				.path = path,
				.directory = rootDirectoryRID,
				.absolutePath = rootDirectory
			});
		}

		Array<PendingAssetLoad> pendingAssetLoads;

		// First pass: scan all directories, only create directory structures and collect file entries
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
				if (extension == ".buffer" || extension == ".buffers") continue;

				String fileName = Path::Name(entry);
				String path = String().Append(scan.path).Append("/").Append(fileName).Append(extension);

				FileStatus status = FileSystem::GetFileStatus(entry);

				if (status.isDirectory)
				{
					RID asset = Resources::Create<ResourceAsset>();
					RID assetFile = Resources::Create<ResourceAssetFile>();

					ResourceObject assetObject = Resources::Write(asset);
					assetObject.SetString(ResourceAsset::Name, fileName);
					assetObject.SetString(ResourceAsset::Extension, extension);
					assetObject.SetString(ResourceAsset::PathId, path);
					assetObject.SetReference(ResourceAsset::Parent, scan.directory);
					assetObject.SetBool(ResourceAsset::Directory, true);
					assetObject.SetReference(ResourceAsset::AssetFile, assetFile);
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
					int loadOrder = INT32_MAX;
					if (auto it = handlersByExtension.Find(extension))
					{
						loadOrder = it->second->GetLoadOrder();
					}

					pendingAssetLoads.EmplaceBack(PendingAssetLoad{
						.directory = scan.directory,
						.path = path,
						.absolutePath = entry,
						.extension = extension,
						.fileName = fileName,
						.fileSize = status.fileSize,
						.lastModifiedTime = status.lastModifiedTime,
						.loadOrder = loadOrder
					});
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

		// Sort pending assets by load order so dependencies (e.g. scripts) are loaded first
		std::sort(pendingAssetLoads.begin(), pendingAssetLoads.end(), [](const PendingAssetLoad& a, const PendingAssetLoad& b)
		{
			return a.loadOrder < b.loadOrder;
		});

		// Second pass: create and load all file assets in order
		for (const PendingAssetLoad& pending : pendingAssetLoads)
		{
			RID asset = Resources::Create<ResourceAsset>();
			RID assetFile = Resources::Create<ResourceAssetFile>();

			ResourceObject assetObject = Resources::Write(asset);
			assetObject.SetString(ResourceAsset::Name, pending.fileName);
			assetObject.SetString(ResourceAsset::Extension, pending.extension);
			assetObject.SetString(ResourceAsset::PathId, pending.path);
			assetObject.SetReference(ResourceAsset::Parent, pending.directory);
			assetObject.SetBool(ResourceAsset::Directory, false);
			assetObject.SetReference(ResourceAsset::AssetFile, assetFile);

			RID object;
			if (auto it = handlersByExtension.Find(pending.extension))
			{
				object = it->second->Load(asset, pending.absolutePath);
			}
			else
			{
				object = Resources::Create<ResourceFile>();
				ResourceObject resourceFileObject = Resources::Write(object);
				resourceFileObject.SetString(ResourceFile::Name, pending.fileName);
				resourceFileObject.Commit();
			}

			if (object)
			{
				assetObject.SetSubObject(ResourceAsset::Object, object);
				Resources::SetPath(object, pending.path);
				ResourceAssets::RegisterAssetByType(object);
			}

			assetObject.Commit();

			ResourceObject assetFileObject = Resources::Write(assetFile);
			assetFileObject.SetReference(ResourceAssetFile::AssetRef, asset);
			assetFileObject.SetString(ResourceAssetFile::AbsolutePath, pending.absolutePath);
			assetFileObject.SetString(ResourceAssetFile::RelativePath, pending.path);
			assetFileObject.SetUInt(ResourceAssetFile::PersistedVersion, Resources::GetVersion(asset));
			assetFileObject.SetUInt(ResourceAssetFile::TotalSizeInDisk, pending.fileSize);
			assetFileObject.SetUInt(ResourceAssetFile::LastModifiedTime, pending.lastModifiedTime);
			assetFileObject.Commit();

			packageObject.AddToSubObjectList(ResourceAssetPackage::Files, assetFile);

			ResourceObject directoryObject = Resources::Write(pending.directory);
			directoryObject.AddToSubObjectList(ResourceAssetDirectory::Assets, asset);
			directoryObject.Commit();

			logger.Debug("asset '{}' loaded", pending.path);
		}

		packageObject.SetSubObject(ResourceAssetPackage::Root, rootDirectoryRID);
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

				ResourceObject        assetObject = Resources::Read(assetToUpdate.asset);
				RID                   object = assetObject.GetSubObject(ResourceAsset::Object);
				ResourceAssetHandler* handler = nullptr;
				if (object)
				{
					if (auto it = handlersByTypeID.Find(Resources::GetType(object)->GetID()))
					{
						handler = it->second;
					}
				}

				if (absolutePath != oldAbsolutePath)
				{
					FileSystem::Rename(MakeAssetBufferPath(oldAbsolutePath), MakeAssetBufferPath(absolutePath));

					if (FileSystem::Rename(oldAbsolutePath, absolutePath))
					{
						logger.Debug("asset moved from {} to {} ", oldAbsolutePath, absolutePath);
					}
					else
					{
						logger.Error("failed to move asset from {} to {} ", oldAbsolutePath, absolutePath);
					}

					if (handler)
					{
						handler->AfterMove(assetToUpdate.asset, oldAbsolutePath, absolutePath);
					}
				}

				if (handler)
				{
					handler->Save(object, absolutePath);
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

				StringView absolutePath = assetFileObject.GetString(ResourceAssetFile::AbsolutePath);
				FileSystem::Remove(absolutePath);
				FileSystem::Remove(Path::Join(Path::Parent(absolutePath), Path::Name(absolutePath).Append(".buffer")));

				String bufferFolder = MakeAssetBufferPath(absolutePath);
				if (FileSystem::GetFileStatus(bufferFolder).exists)
				{
					for (const String& file : DirectoryEntries(bufferFolder))
					{
						String bufferAssetTempPath = Path::Join(bufferTempFolder, Path::Name(file).Append(".buffer"));
						FileSystem::Rename(file, bufferAssetTempPath);
					}
					FileSystem::Remove(bufferFolder);
				}
				logger.Debug("asset file removed from {} ", absolutePath);

				Resources::Destroy(assetToUpdate.assetFile);
			}
		}

		packageObject.Commit();
	}

	Array<UpdatedAssetInfo> ResourceAssets::GetUpdatedAssets(RID directory)
	{
		Array<UpdatedAssetInfo> items;

		ResourceObject packageObject = Resources::Read(directory);
		Span<RID>      files = packageObject.GetSubObjectList(ResourceAssetPackage::Files);
		for (RID assetFile : files)
		{
			ResourceObject assetFileObject = Resources::Read(assetFile);
			StringView     absolutePath = assetFileObject.GetString(ResourceAssetFile::AbsolutePath);
			RID            asset = assetFileObject.GetReference(ResourceAssetFile::AssetRef);
			ResourceObject assetObject = Resources::Read(asset);

			if (assetObject.GetBool(ResourceAsset::ReadOnly))
			{
				continue;
			}

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

				if (assetObject.GetBool(ResourceAsset::ReadOnly))
				{
					return;
				}

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
				checkAssetFile(GetAssetPayload(rid));
				directoriesToScan.emplace(rid);
			});
		}
		return items;
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
			Platform::OpenURL(GetAbsolutePath(rid));
		}
	}

	void ResourceAssets::ImportAsset(RID parent, StringView path)
	{
		if (!FileSystem::GetFileStatus(path).isDirectory)
		{
			pendingImports.EmplaceBack(parent, path);
			return;
		}

		for (const String& file : DirectoryEntries(path))
		{
			pendingImports.EmplaceBack(parent, file);
		}
	}

	RID ResourceAssets::CreateAsset(RID parent, TypeID typeId, StringView desiredName, UndoRedoScope* scope)
	{
		return CreateImportedAsset(parent, typeId, desiredName, scope, "");
	}

	RID ResourceAssets::CreateAssetFile(RID parent, StringView desiredName, String extension, UndoRedoScope* scope)
	{
		String name = CreateUniqueAssetName(parent, desiredName.Empty() ? String("New File") : String(desiredName), extension, false);
		String path = GetDirectoryPathId(parent) + "/" + name + extension;

		RID asset = Resources::Create<ResourceAssetFile>({}, scope);

		ResourceObject object = Resources::Write(asset);
		object.SetString(ResourceAsset::Name, name);
		object.SetString(ResourceAsset::Extension, extension);
		object.SetSubObject(ResourceAsset::Object, asset);
		object.SetReference(ResourceAsset::Parent, parent);
		object.SetString(ResourceAsset::PathId, path);
		object.SetBool(ResourceAsset::Directory, false);
		object.Commit(scope);

		ResourceObject parentObject = Resources::Write(parent);
		parentObject.AddToSubObjectList(ResourceAssetDirectory::Assets, asset);
		parentObject.Commit(scope);

		return asset;
	}

	RID ResourceAssets::DuplicateAsset(RID parent, RID sourceAsset, StringView desiredName, UndoRedoScope* scope)
	{
		if (ResourceAssetHandler* handler = GetAssetHandler(sourceAsset))
		{
			String newName = CreateUniqueAssetName(parent, desiredName.Empty() ? GetAssetName(sourceAsset) : String(desiredName), handler->Extension(), false);
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

			RegisterAssetByType(asset);

			logger.Debug("asset from type {} created with uuid {} name {} ", handler->GetDesc(), Resources::GetUUID(asset).ToString(), newName);

			return asset;
		}

		return {};
	}

	RID ResourceAssets::CreateInheritedAsset(RID parent, RID sourceAsset, StringView desiredName, UndoRedoScope* scope)
	{
		if (ResourceAssetHandler* handler = GetAssetHandler(sourceAsset))
		{
			String newName = CreateUniqueAssetName(parent, desiredName.Empty() ? GetAssetName(sourceAsset).Append(" (Inherited)") : String(desiredName), handler->Extension(), false);
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

			String newName = CreateUniqueAssetName(parent, desiredName.Empty() ? String("New ").Append(handler->GetDesc()) : String(desiredName), handler->Extension(), false);
			String path = GetDirectoryPathId(parent) + "/" + newName + handler->Extension();

			RID asset = handler->Create(UUID::RandomUUID(), scope);

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

			RegisterAssetByType(asset);

			logger.Debug("asset from type {} created with uuid {} name {} ", handler->GetDesc(), Resources::GetUUID(asset).ToString(), newName);

			return asset;
		}

		logger.Error("asset from type {} cannot be created, no handler found for it", typeId);
		return {};
	}

	RID ResourceAssets::CreateExtractedAsset(RID parent, TypeID typeId, StringView desiredName, RID object, UndoRedoScope* scope)
	{
		if (auto it = handlersByTypeID.Find(typeId))
		{
			ResourceAssetHandler* handler = it->second;

			String newName = CreateUniqueAssetName(parent, desiredName.Empty() ? String("New ").Append(handler->GetDesc()) : String(desiredName), handler->Extension(), false);
			String path = GetDirectoryPathId(parent) + "/" + newName + handler->Extension();

			RID rid = Resources::Create<ResourceAsset>(UUID::RandomUUID(), scope);

			ResourceObject assetObject = Resources::Write(rid);
			assetObject.SetString(ResourceAsset::Name, newName);
			assetObject.SetString(ResourceAsset::Extension, handler->Extension());
			assetObject.SetSubObject(ResourceAsset::Object, object);
			assetObject.SetReference(ResourceAsset::Parent, parent);
			assetObject.SetString(ResourceAsset::PathId, path);
			assetObject.SetBool(ResourceAsset::Directory, false);
			assetObject.Commit(scope);

			ResourceObject parentObject = Resources::Write(parent);
			parentObject.AddToSubObjectList(ResourceAssetDirectory::Assets, rid);
			parentObject.Commit(scope);

			RegisterAssetByType(object);

			logger.Debug("extracted asset from type {} created with uuid {} name {} ", handler->GetDesc(), Resources::GetUUID(object).ToString(), newName);

			return rid;
		}

		logger.Error("extracted asset from type {} cannot be created, no handler found for it", typeId);
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

		for (RID child : parentObject.GetSubObjectList(ResourceAssetDirectory::Assets))
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
		String newName = CreateUniqueAssetName(parent, desiredName, "", true);
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

	String ResourceAssets::CreateUniqueAssetName(RID parent, StringView desiredName, StringView extension, bool directory)
	{
		if (!parent) return {};

		ResourceObject parentObject = Resources::Read(parent);
		Array          children = parentObject.GetSubObjectList(directory ? ResourceAssetDirectory::Directories : ResourceAssetDirectory::Assets);

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

				if (childObject && finalName == childObject.GetString(ResourceAsset::Name) && extension == childObject.GetString(ResourceAsset::Extension))
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
		bool           isDirectory = assetObject.GetBool(ResourceAsset::Directory);
		String         newName = CreateUniqueAssetName(newParent, assetObject.GetString(ResourceAsset::Name), assetObject.GetString(ResourceAsset::Extension), isDirectory);

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
		RID           resourceAsset = asset;
		ResourceType* type = Resources::GetStorage(asset)->resourceType;
		if (type == nullptr) return "";

		if (type->GetID() != TypeInfo<ResourceAsset>::ID())
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

	RID ResourceAssets::GetAssetPayload(RID rid)
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
		u32        i = 0;
		Split(pathId, StringView{":/"}, [&](StringView path)
		{
			if (i > 2) return;
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
		if (ResourceObject directoryObject = Resources::Read(rid))
		{
			if (directoryObject.GetBool(ResourceAsset::Directory))
			{
				return Resources::GetParent(Resources::GetParent(rid));
			}
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
		//
		// if (assetObject.GetBool(ResourceAsset::ReadOnly))
		// {
		// 	return false;
		// }

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

	u64 ResourceAssets::GetAssetObjectVersion(RID rid)
	{
		if (ResourceObject assetObject = Resources::Read(rid))
		{
			if (assetObject.GetType()->GetID() == TypeInfo<ResourceAsset>::ID())
			{
				if (RID object = assetObject.GetSubObject(ResourceAsset::Object))
				{
					return Resources::GetVersion(object);
				}
			}
			else
			{
				return assetObject.GetVersion();
			}
		}
		return 0;
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

	void ResourceAssets::ExportPackages(Span<RID> packages, StringView path, StringView name)
	{
		BinaryArchiveWriter writer;

		writer.BeginMap("projectSettings");
		Settings::Save(writer, TypeInfo<ProjectSettings>::ID());
		writer.EndMap();

		// Collect all assets with their load order before writing
		struct AssetToExport
		{
			RID asset;
			int loadOrder;
		};
		Array<AssetToExport> assetsToExport;

		for (RID package : packages)
		{
			ResourceObject packageObject = Resources::Read(package);

			std::queue<RID> directoriesToScan;
			directoriesToScan.emplace(packageObject.GetSubObject(ResourceAssetPackage::Root));

			while (!directoriesToScan.empty())
			{
				RID rid = directoriesToScan.front();
				directoriesToScan.pop();
				ResourceObject directoryObject = Resources::Read(rid);

				directoryObject.IterateSubObjectList(ResourceAssetDirectory::Assets, [&](RID asset)
				{
					ResourceObject assetObject = Resources::Read(asset);
					if (assetObject.GetSubObject(ResourceAsset::Object))
					{
						int loadOrder = INT32_MAX;
						if (String extension = assetObject.GetString(ResourceAsset::Extension); !extension.Empty())
						{
							if (auto it = handlersByExtension.Find(extension))
							{
								loadOrder = it->second->GetLoadOrder();
							}
						}
						assetsToExport.EmplaceBack(AssetToExport{asset, loadOrder});
					}
				});
				directoryObject.IterateSubObjectList(ResourceAssetDirectory::Directories, [&](RID rid)
				{
					directoriesToScan.emplace(rid);
				});
			}
		}

		// Sort by load order so dependencies (e.g. scripts) are exported first
		std::sort(assetsToExport.begin(), assetsToExport.end(), [](const AssetToExport& a, const AssetToExport& b)
		{
			return a.loadOrder < b.loadOrder;
		});

		writer.BeginSeq("assets");

		String resourceFile = Path::Join(path, String(name).Append(SK_BUFFER_EXT));
		u64    offset = 0;

		ByteBuffer byteBuffer;
		byteBuffer.Resize(10000);

		FileHandler bufferHandler = FileSystem::OpenFile(resourceFile, AccessMode::WriteOnly);

		for (const AssetToExport& entry : assetsToExport)
		{
			ResourceObject assetObject = Resources::Read(entry.asset);
			RID            object = assetObject.GetSubObject(ResourceAsset::Object);

			writer.BeginMap();
			{
				writer.WriteString("pathId", GetPathId(entry.asset));

				if (ResourceAssetHandler* handler = GetAssetHandler(object))
				{
					handler->Export(object, writer);
				}
				else
				{
					Resources::Serialize(object, writer);
				}

				if (ResourceObject assetFileObject = Resources::Read(object))
				{
					u32 bufferCount = 0;

					assetFileObject.IterateAllBuffers([&](const ResourceBuffer& buffer)
					{
						if (bufferCount == 0)
						{
							writer.BeginSeq("buffers");
						}

						u64 bufferSize = buffer.GetSize();
						if (buffer.GetSize() > byteBuffer.Size())
						{
							byteBuffer.Resize(buffer.GetSize());
						}
						buffer.CopyData(byteBuffer.begin(), bufferSize);

						writer.BeginMap();
						writer.WriteString("id", buffer.GetIdAsString());
						writer.WriteUInt("offset", offset);
						writer.WriteUInt("size", bufferSize);
						writer.EndMap();

						offset += FileSystem::WriteFile(bufferHandler, byteBuffer.begin(), bufferSize);

						bufferCount++;
					});

					if (bufferCount > 0)
					{
						writer.EndSeq();
					}
					writer.WriteUInt("bufferCount", bufferCount);
				}
			}
			writer.EndMap();
		}

		FileSystem::CloseFile(bufferHandler);

		writer.EndSeq();

		Span<u8>  uncompressedData = writer.GetData();
		usize     compressedSize = Compression::GetMaxCompressedBufferSize(uncompressedData.Size(), CompressionMode::ZSTD);
		Array<u8> compressedData(MemoryGlobals::GetHeapAllocator());
		compressedData.Resize(compressedSize);

		compressedSize = Compression::Compress(compressedData.Data(), compressedSize, uncompressedData.Data(), uncompressedData.Size(), CompressionMode::ZSTD);
		FileSystem::SaveFileAsByteArray(Path::Join(path, String(name) + SK_RESOURCE_EXT), Span(compressedData.Data(), compressedSize));
	}

	GPUTexture* ResourceAssets::GetThumbnail(RID rid)
	{
		bool needsUpdate = false;

		GPUTexture* texture = assertTexture;
		{
			//check if it's already loaded.
			std::unique_lock lock(thumbnailsMutex);
			if (const auto it = thumbnails.Find(rid))
			{
				if (it->second.canUpdate && it->second.lastCheck + 0.5f < currentTime)
				{
					u64 currentVersion = GetAssetObjectVersion(rid);
					needsUpdate = it->second.version != currentVersion;
					it->second.version = currentVersion;
					it->second.lastCheck = currentTime;
				}

				if (it->second.texture != nullptr)
				{
					texture = it->second.texture;
				}

				if (it->second.forceUpdate)
				{
					it->second.forceUpdate = false;
					needsUpdate = true;
				}

				if (!needsUpdate)
				{
					return texture;
				}
			}
		}

		//not loaded, create empty one
		if (ResourceAssetHandler* handler = GetAssetHandler(rid))
		{
			if (FnThumbnailGenerator generator = handler->GetThumbnailGenerator(rid))
			{
				std::unique_lock lock(thumbnailsMutex);
				if (auto it = thumbnails.Find(rid); it == thumbnails.end())
				{
					thumbnails.Insert(rid, ThumbnailData{
						                  .texture = nullptr,
						                  .version = GetAssetObjectVersion(rid),
						                  .lastCheck = currentTime,
						                  .canUpdate = generator != nullptr
					                  });
				}

				if (!needsUpdate && HasThumbnail(rid))
				{
					Editor::AddTask([rid]
					{
						UpdateThumbnailData(rid);
					});
				}
				else if (generator)
				{
					Editor::AddTask([generator, rid]
					                {
						                generator(rid);
					                },
					                "Generating Thumbnail");
				}
			}

			if (handler->GetIcon() != nullptr)
			{
				return nullptr;
			}
		}
		return texture;
	}

	GPUTexture* ResourceAssets::GetDefaultThumbnail()
	{
		return assertTexture;
	}

	const char* ResourceAssets::GetIcon(RID rid)
	{
		if (ResourceAssetHandler* handler = GetAssetHandler(rid))
		{
			return handler->GetIcon();
		}
		return nullptr;
	}

	void ResourceAssets::UpdateThumbnail(RID rid, Span<u8> data)
	{
		usize     compressedSize = Compression::GetMaxCompressedBufferSize(data.Size(), CompressionMode::ZSTD);
		Array<u8> compressedData;
		compressedData.Resize(compressedSize);
		compressedSize = Compression::Compress(compressedData.Data(), compressedSize, data.Data(), data.Size(), CompressionMode::ZSTD);
		compressedData.Resize(compressedSize);
		compressedData.ShrinkToFit();
		String thumbnailPath = Path::Join(thumbnailDirectory, GetAssetUUID(rid).ToString());

		FileSystem::SaveFileAsByteArray(thumbnailPath, compressedData);

		UpdateThumbnailData(rid);
	}

	ResourceBuffer ResourceAssets::CreateTempBuffer()
	{
		return CreateTempBuffer(nullptr, 0);
	}

	ResourceBuffer ResourceAssets::CreateTempBuffer(VoidPtr bytes, usize size)
	{
		ResourceBuffer buffer = ResourceBuffer(Random::Xorshift64star());
		String         bufferPath = Path::Join(bufferTempFolder, buffer.GetIdAsString().Append(".buffer"));
		if (size > 0 && bytes)
		{
			FileSystem::SaveFileAsByteArray(bufferPath, Span{static_cast<u8*>(bytes), size});
		}
		buffer.MapFile(bufferPath, false, 0, 0);
		return buffer;
	}

	void ResourceAssets::RegisterAssetByType(RID asset)
	{
		ResourceType* type = Resources::GetType(asset);
		if (!type) return;
		std::unique_lock lock(assetsByTypeMutex);
		assetsByType[type->GetID()].Insert(asset);
	}

	void ResourceAssets::UnregisterAssetByType(RID asset)
	{
		ResourceType* type = Resources::GetType(asset);
		if (!type) return;
		std::unique_lock lock(assetsByTypeMutex);
		if (auto it = assetsByType.Find(type->GetID()); it != assetsByType.end())
		{
			it->second.Erase(asset);
		}
	}

	Array<RID> ResourceAssets::GetAssets(TypeID typeId)
	{
		std::unique_lock lock(assetsByTypeMutex);
		if (auto it = assetsByType.Find(typeId); it != assetsByType.end())
		{
			Array<RID> result;
			result.Reserve(it->second.Size());
			for (RID rid : it->second)
			{
				result.EmplaceBack(rid);
			}
			return result;
		}
		return {};
	}

	RID ResourceAssets::GetResourceAssetFromResourceRecursive(RID rid)
	{
		if (!rid) return {};
		RID current = rid;
		while (Resources::GetStorage(current)->resourceType->GetID() != TypeInfo<ResourceAsset>::ID())
		{
			current = Resources::GetParent(current);
			if (!current)
			{
				return {};
			}
		}
		return current;
	}

	void ResourceAssetsUpdate()
	{
		currentTime += App::DeltaTime();

		if (!pendingImports.Empty())
		{
			UndoRedoScope* scope = Editor::CreateUndoRedoScope("Import Assets");

			for (const AssetsPendingImport& toImport : pendingImports)
			{
				logger.Debug("importing {} to {} ", toImport.path, ResourceAssets::GetDirectoryPathId(toImport.parent));
				String extension = Path::Extension(toImport.path);
				extension = extension.ToLowerCase();

				if (auto it = importersByExtension.Find(extension))
				{
					ResourceAssetImporter* importer = it->second;

					auto func = [importer, toImport, scope]
					{
						importer->ImportAsset(toImport.parent, nullptr, toImport.path, scope);
					};

					//	- hard to check if asset with name is already imported.
					Editor::AddTask(func, String("Importing Asset ").Append(Path::Name(toImport.path)));
				}
			}
			pendingImports.Clear();
		}

		{
			auto          now = std::chrono::steady_clock::now();
			Array<String> readyPaths;

			{
				std::unique_lock lock(fileListener.mutex);
				for (auto it = fileListener.pendingEvents.begin(); it != fileListener.pendingEvents.end(); ++it)
				{
					if (now - it->second.timestamp >= fileWatchDebounceTime)
					{
						readyPaths.EmplaceBack(it->first);
					}
				}

				for (const String& path : readyPaths)
				{
					fileListener.pendingEvents.Erase(path);
				}
			}

			for (const String& eventPath : readyPaths)
			{
				for (auto it = loadedPackages.begin(); it != loadedPackages.end(); ++it)
				{
					String rootDir = it->second;
					for (usize i = 0; i < rootDir.Size(); ++i)
					{
						if (rootDir[i] == '\\') rootDir[i] = '/';
					}

					StringView eventPathView = eventPath;
					if (eventPathView.StartsWith(rootDir))
					{
						StringView relativePath = StringView(eventPath.CStr() + rootDir.Size(), eventPath.Size() - rootDir.Size());
						String     pathId = String(it->first).Append(":/").Append(relativePath);

						if (RID object = Resources::FindByPath(pathId))
						{
							RID asset = Resources::GetParent(object);
							if (ResourceAssetHandler* handler = ResourceAssets::GetAssetHandler(asset))
							{
								logger.Debug("updating asset {}", pathId);
								handler->Reloaded(asset, eventPath);
							}
						}
						break;
					}
				}
			}
		}
	}

	void ResourceAssetsShutdown()
	{
		for (auto& it : handlersByHandlerType)
		{
			DestroyAndFree(it.second);
		}

		for (auto& it : importersByImporterType)
		{
			DestroyAndFree(it.second);
		}

		for (auto& it : thumbnails)
		{
			if (it.second.texture)
			{
				it.second.texture->Destroy();
			}
		}

		handlersByExtension.Clear();
		handlersByTypeID.Clear();
		handlersByHandlerType.Clear();

		importersByExtension.Clear();
		importersByImporterType.Clear();

		if (efswWatcher)
		{
			DestroyAndFree(efswWatcher);
			efswWatcher = nullptr;
		}
		assertTexture->Destroy();
	}


	void ReloadAssetHandlers()
	{
		for (TypeID derivedId : Reflection::GetDerivedTypes(TypeInfo<ResourceAssetHandler>::ID()))
		{
			ReflectType* type = Reflection::FindTypeById(derivedId);
			if (!type)
			{
				continue;
			}

			Object* newObject = type->NewObject();
			if (!newObject)
			{
				continue;
			}

			ResourceAssetHandler* handler = newObject->SafeCast<ResourceAssetHandler>();
			if (!handler)
			{
				DestroyAndFree(newObject);
				continue;
			}

			if (auto it = handlersByHandlerType.Find(derivedId))
			{
				ResourceAssetHandler* previous = it->second;

				if (StringView extension = previous->Extension(); !extension.Empty())
				{
					handlersByExtension.Erase(extension);
				}

				if (TypeID typeId = previous->GetResourceTypeId())
				{
					handlersByTypeID.Erase(typeId);
				}

				DestroyAndFree(previous);
				it->second = handler;
			}
			else
			{
				handlersByHandlerType.Insert(derivedId, handler);
			}

			logger.Debug("Registered asset handler {} for extension {} ", type->GetName(), handler->Extension());

			if (StringView extension = handler->Extension(); !extension.Empty())
			{
				handlersByExtension.Insert(extension, handler);
			}

			if (TypeID typeId = handler->GetResourceTypeId())
			{
				handlersByTypeID.Insert(typeId, handler);
			}
		}

		for (TypeID derivedId : Reflection::GetDerivedTypes(TypeInfo<ResourceAssetImporter>::ID()))
		{
			ReflectType* type = Reflection::FindTypeById(derivedId);
			if (!type)
			{
				continue;
			}

			Object* newObject = type->NewObject();
			if (!newObject)
			{
				continue;
			}

			ResourceAssetImporter* importer = newObject->SafeCast<ResourceAssetImporter>();
			if (!importer)
			{
				DestroyAndFree(newObject);
				continue;
			}

			if (auto it = importersByImporterType.Find(derivedId))
			{
				ResourceAssetImporter* previous = it->second;

				for (const String& extension : previous->ImportedExtensions())
				{
					importersByExtension.Erase(extension);
				}

				DestroyAndFree(previous);
				it->second = importer;
			}
			else
			{
				importersByImporterType.Insert(derivedId, importer);
			}

			for (const String& extension : importer->ImportedExtensions())
			{
				logger.Debug("Registered asset importer {} for extension {} ", type->GetName(), extension);
				importersByExtension.Insert(extension, importer);
			}
		}
	}

	void ResourceAssetsInit()
	{
		Event::Bind<OnShutdown, ResourceAssetsShutdown>();
		Event::Bind<OnUpdate, ResourceAssetsUpdate>();
		Event::Bind<OnReflectionUpdated, ReloadAssetHandlers>();

		assertTexture = StaticContent::GetTexture("Content/Images/FileIcon.png");

		bufferTempFolder = Path::Join(Editor::GetProjectTempPath(), "Buffers");
		if (FileSystem::GetFileStatus(bufferTempFolder).exists)
		{
			FileSystem::Remove(bufferTempFolder);
		}
		FileSystem::CreateDirectory(bufferTempFolder);

		String cacheDirectory = Path::Join(Editor::GetProjectLocalPath(), "Cache");
		if (!FileSystem::GetFileStatus(cacheDirectory).exists)
		{
			FileSystem::CreateDirectory(cacheDirectory);
		}

		thumbnailDirectory = Path::Join(cacheDirectory, "Thumbnails");
		if (!FileSystem::GetFileStatus(thumbnailDirectory).exists)
		{
			FileSystem::CreateDirectory(thumbnailDirectory);
		}

		ReloadAssetHandlers();

		efswWatcher = Alloc<efsw::FileWatcher>();
		efswWatcher->watch();
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

	void RegisterAudioHandler();
	void RegisterEntityHandler();
	void RegisterSceneHandler();
	void RegisterMaterialHandler();
	void RegisterTextureHandler();
	void RegisterMeshHandler();
	void RegisterShaderHandler();
	void RegisterFontHandler();
	void RegisterAnimationHandler();
	void RegisterAnimationControllerHandler();
	void RegisterDCCAssetHandler();

	void RegisterAudioImporter();
	void RegisterTextureImporter();
	void RegisterFBXImporter();
	void RegisterGLTFImporter();
	void RegisterObjImporter();
	void RegisterFontImporter();

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
			.Field<ResourceAsset::ReadOnly>(ResourceFieldType::Bool)
			.Build();

		Resources::FindType<ResourceAsset>()->RegisterEvent(ResourceEventType::Changed, OnUpdateAsset, nullptr);

		RegisterAudioHandler();
		RegisterEntityHandler();
		RegisterSceneHandler();
		RegisterTextureHandler();
		RegisterMaterialHandler();
		RegisterMeshHandler();
		RegisterShaderHandler();
		RegisterFontHandler();
		RegisterAnimationHandler();
		RegisterAnimationControllerHandler();
		RegisterDCCAssetHandler();

		RegisterAudioImporter();
		RegisterTextureImporter();
		RegisterFBXImporter();
		RegisterGLTFImporter();
		RegisterObjImporter();
		RegisterFontImporter();
	}
}
