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


#include "AssetTypes.hpp"

#include "AssetFileOld.hpp"
#include "Skore/Core/Reflection.hpp"
#include "Skore/IO/Assets.hpp"
#include "Skore/IO/FileSystem.hpp"

namespace Skore
{
	void RegisterSceneAssetHandler();
	void RegisterGLTFImporter();
	void RegisterTextureHandler2();
	void RegisterMaterialAssetHandler();
	void RegisterMeshAssetHandler();
	void RegisterShaderHandler();
	void RegisterFBXImporter();

	void RegisterAssetTypes()
	{
		Reflection::Type<AssetImporter>();
		Reflection::Type<AssetHandler>();
		Reflection::Type<AssetInfo>();
		Reflection::Type<AssetInternalInfo>();

		RegisterSceneAssetHandler();
		RegisterTextureHandler2();
		RegisterGLTFImporter();
		RegisterMaterialAssetHandler();
		RegisterMeshAssetHandler();
		RegisterShaderHandler();
		RegisterFBXImporter();


		auto assetStatus = Reflection::Type<AssetStatus>();
		assetStatus.Value<AssetStatus::None>("None");
		assetStatus.Value<AssetStatus::Error>("Error");
		assetStatus.Value<AssetStatus::Warning>("Warning");
	}

	void AssetHandler::LoadInstance(AssetFileOld* assetFile, Asset* asset)
	{
		AssetFileType type = assetFile->GetAssetTypeFile();
		if (type == AssetFileType::Asset)
		{
			String str = FileSystem::ReadFileAsString(assetFile->GetAbsolutePath());

			YamlArchiveReader yamlArchiveReader(str);
			asset->Deserialize(yamlArchiveReader);
		}
		else if (type == AssetFileType::ImportedAsset || type == AssetFileType::Child)
		{
			Array<u8> data(assetFile->GetImportedSize());

			String file = assetFile->GetImportAssetFile();
			FileHandler handler = FileSystem::OpenFile(file, AccessMode::ReadOnly);
			FileSystem::ReadFileAt(handler, data.Data(), assetFile->GetImportedSize(), assetFile->GetImportedOffset());
			FileSystem::CloseFile(handler);

			BinaryArchiveReader reader(data);
			asset->Deserialize(reader);
		}
	}

	void AssetInfo::RegisterType(NativeReflectType<AssetInfo>& type)
	{
		type.Field<&AssetInfo::uuid>("uuid");
		type.Field<&AssetInfo::type>("type");
		type.Field<&AssetInfo::name>("name");
		type.Field<&AssetInfo::offset>("offset");
		type.Field<&AssetInfo::size>("size");
	}

	void AssetInternalInfo::RegisterType(NativeReflectType<AssetInternalInfo>& type)
	{
		type.Field<&AssetInternalInfo::uuid>("uuid");
		type.Field<&AssetInternalInfo::status>("status");
		type.Field<&AssetInternalInfo::missingFiles>("missingFiles");
	}
}
