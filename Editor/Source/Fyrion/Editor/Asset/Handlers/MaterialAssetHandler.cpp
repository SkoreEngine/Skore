#include "JsonAssetHandler.hpp"
#include "Fyrion/Editor/EditorTypes.hpp"
#include "Fyrion/Editor/Asset/AssetEditor.hpp"
#include "Fyrion/Graphics/Assets/MaterialAsset.hpp"

namespace Fyrion
{
    struct MaterialAssetHandler : JsonAssetHandler
    {
        FY_BASE_TYPES(JsonAssetHandler);

        StringView Extension() override
        {
            return ".material";
        }

        TypeID GetAssetTypeID() override
        {
            return GetTypeID<MaterialAsset>();
        }

        void OpenAsset(AssetFile* assetFile) override
        {
            onAssetSelectionHandler.Invoke(assetFile);
        }

        Image GenerateThumbnail(AssetFile* assetFile) override
        {
            return {};
        }

        static inline EventHandler<OnAssetSelection> onAssetSelectionHandler{};
    };

    void RegisterMaterialAssetHandler()
    {
        Registry::Type<MaterialAssetHandler>();
    }
}
