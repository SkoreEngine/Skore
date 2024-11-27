#pragma once

#include "Skore/Editor/Asset/AssetTypes.hpp"

namespace Skore
{
    struct JsonAssetHandler : AssetHandler
    {
        SK_BASE_TYPES(AssetHandler);

        void Save(StringView newPath, AssetFile* assetFile) override;
        void Load(AssetFile* assetFile, TypeHandler* typeHandler, VoidPtr instance) override;
    };
}
