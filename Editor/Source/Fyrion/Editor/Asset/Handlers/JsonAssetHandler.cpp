#include "JsonAssetHandler.hpp"

#include "Fyrion/Core/Serialization.hpp"
#include "Fyrion/Editor/Asset/AssetEditor.hpp"
#include "Fyrion/IO/FileSystem.hpp"

namespace Fyrion
{
    void JsonAssetHandler::Save(StringView newPath, AssetFile* assetFile)
    {
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
            Serialization::Deserialize(typeHandler, reader, reader.GetRootObject(), instance);
        }
    }
}
