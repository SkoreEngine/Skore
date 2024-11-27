#pragma once
#include "Skore/Core/Array.hpp"
#include "Skore/Core/Image.hpp"
#include "Skore/Core/Registry.hpp"

namespace Skore
{
    struct AssetFile;

    struct SK_API AssetImporter
    {
        virtual ~AssetImporter() = default;

        virtual Array<String> ImportExtensions() = 0;
        virtual bool          ImportAsset(AssetFile* parent, StringView path) = 0;
    };

    struct SK_API AssetHandler
    {
        virtual            ~AssetHandler() = default;
        virtual StringView Extension() = 0;
        virtual TypeID     GetAssetTypeID() = 0;
        virtual void       Save(StringView newPath, AssetFile* assetFile) = 0;
        virtual void       Load(AssetFile* assetFile, TypeHandler* typeHandler, VoidPtr instance) = 0;
        virtual void       OpenAsset(AssetFile* assetFile) = 0;
        virtual Image      GenerateThumbnail(AssetFile* assetFile) = 0;
    };
}
