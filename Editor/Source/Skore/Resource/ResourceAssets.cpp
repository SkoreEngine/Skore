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

#include "ResourceAssetTypes.hpp"
#include "Skore/EditorCommon.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Queue.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/IO/FileSystem.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Resource/Resources.hpp"

namespace Skore
{
	class Logger;

	struct FilesToScan
	{
		String path;
		String absolutePath;
		RID    parent;
	};

	namespace
	{
		RID     projectRID;
		Logger& logger = Logger::GetLogger("Skore::ResourceAssets", LogLevel::Debug);

		RID ScanForAssets(RID parent, StringView name, StringView path)
		{
			Queue<FilesToScan> pendingItems(100);

			std::optional<FilesToScan> currentScanItem = std::make_optional(FilesToScan{
				.absolutePath = path,
				.parent = parent
			});

			RID first = {};

			while (currentScanItem.has_value())
			{
				const FilesToScan& item = currentScanItem.value();
				StringView extension = Path::Extension(item.absolutePath);

				do
				{
					if (extension == SK_IMPORT_EXTENSION ||
						extension == SK_INFO_EXTENSION ||
						extension == SK_PROJECT_EXTENSION)
					{
						break;
					}

					// if (ignoredExtensions.Has(extension))
					// {
					// 	break;
					// }

					// if (GetFileByAbsolutePath(item.absolutePath))
					// {
					// 	break;
					// }

					FileStatus status = FileSystem::GetFileStatus(item.absolutePath);

					String fileName = item.parent ? Path::Name(item.absolutePath) : String(name);

					RID asset = Resources::Create<ResourceAsset>();

					ResourceObject assetObject = Resources::Write(asset);
					assetObject.SetString(ResourceAsset::Name, fileName);
					assetObject.SetString(ResourceAsset::Extension, extension);
					assetObject.SetString(ResourceAsset::AbsolutePath, item.absolutePath);
					assetObject.SetReference(ResourceAsset::Parent, item.parent);
					assetObject.SetBool(ResourceAsset::IsDirectory, status.isDirectory);

					if (!first)
					{
						first = asset;
					}

					if (status.isDirectory)
					{

						if (item.parent)
						{
							ResourceObject parentObject = Resources::Write(item.parent);
							parentObject.AddToReferenceArray(ResourceAsset::Children, asset);
							parentObject.Commit();
						}
						logger.Debug("directory {} registered successfully", fileName);

						for (const String& entry : DirectoryEntries(item.absolutePath))
						{
							pendingItems.Enqueue(FilesToScan{
								.absolutePath = entry,
								.parent = asset
							});
						}
					}

					if (!status.isDirectory)
					{

					}

					assetObject.Commit();


					// AssetFile* assetFile = CreateAssetFile(item.parent, item.parent != nullptr ? Path::Name(item.absolutePath) : String(name), extension);
					// if (first == nullptr)
					// {
					// 	first = assetFile;
					// }

					//assetFile->UpdateAbsolutePath(item.absolutePath);
					//fileWatcher.Watch(assetFile, item.absolutePath);

					if (!status.isDirectory)
					{
						// if (const auto& it = handlersByExtension.Find(assetFile->m_extension))
						// {
						// 	assetFile->m_type = AssetFileType::Asset;
						// 	assetFile->m_handler = it->second.Get();
						// }
						// else if (const auto& it = extensionImporters.Find(extension))
						// {
						// 	assetFile->m_type = AssetFileType::ImportedAsset;
						// 	assetFile->m_importer = it->second.Get();
						//
						// 	if (const auto& itHandler = handlersByTypeID.Find(assetFile->m_importer->GetAssetTypeId()))
						// 	{
						// 		assetFile->m_handler = itHandler->second.Get();
						// 	}
						// }
						// else
						// {
						// 	assetFile->m_type = AssetFileType::Other;
						// }
						//
						// AddAssetFile(assetFile);
						// assetFile->Register();
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
	}


	void ResourceAssets::SetProject(StringView name, StringView directory)
	{
		String libAssets = Path::Join(directory, "Assets");
		projectRID = ScanForAssets({}, name, libAssets);
	}

	void ResourceAssets::AddPackage(StringView name, StringView directory)
	{

	}

	RID ResourceAssets::GetProject()
	{
		return projectRID;
	}

	Span<RID> ResourceAssets::GetPackages()
	{
		return {};
	}

	RID ResourceAssets::CreateDirectory(RID parent, StringView desiredName, UndoRedoScope* scope)
	{
		//String newName = CreateUniqueName(parent, desiredName, true);
		return {};
	}

	RID ResourceAssets::FindOrCreateAsset(RID parent, TypeID typeId, StringView suggestedName)
	{
		return {};
	}

	RID ResourceAssets::CreateAsset(RID parent, TypeID typeId, StringView suggestedName, UUID uuid)
	{
		return {};
	}

	String ResourceAssets::CreateUniqueName(RID parent, StringView desiredName)
	{
		return {};
	}

	void ResourceAssets::GetUpdatedAssets(Array<RID>& updatedAssets)
	{
		//TODO
	}

	Span<RID> ResourceAssets::GetAssetsByType(TypeID typeId)
	{
		return {};
	}

	RID ResourceAssets::GetAssetByAbsolutePath(StringView path)
	{
		return {};
	}
}
