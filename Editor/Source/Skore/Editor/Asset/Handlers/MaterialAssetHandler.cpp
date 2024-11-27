#include "JsonAssetHandler.hpp"
#include "Skore/Editor/EditorTypes.hpp"
#include "Skore/Editor/Asset/AssetEditor.hpp"
#include "Skore/Graphics/Assets/MaterialAsset.hpp"

namespace Skore
{
    struct MaterialAssetHandler : JsonAssetHandler
    {
        SK_BASE_TYPES(JsonAssetHandler);

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
