#include "JsonAssetHandler.hpp"

#include "Skore/Core/Serialization.hpp"
#include "Skore/Editor/Asset/AssetEditor.hpp"
#include "Skore/IO/FileSystem.hpp"

namespace Skore
{
    void JsonAssetHandler::Save(StringView newPath, AssetFile* assetFile)
    {
        if (assetFile->IsNewAsset())
        {
            Assets::Load(assetFile->uuid);
        }

        if (Asset* asset = Assets::Get(assetFile->uuid))
        {
            JsonArchiveWriter writer;
            ArchiveValue      assetArchive = Serialization::Serialize(GetAssetTypeID(), writer, asset);
            FileSystem::SaveFileAsString(newPath, JsonArchiveWriter::Stringify(assetArchive));
        }
    }

    void JsonAssetHandler::Load(AssetFile* assetFile, TypeHandler* typeHandler, VoidPtr instance)
    {
        if (String str = FileSystem::ReadFileAsString(assetFile->absolutePath); !str.Empty())
        {
            JsonArchiveReader reader(str);
            Serialization::Deserialize(typeHandler, reader, reader.GetRoot(), instance);
        }
    }
}
