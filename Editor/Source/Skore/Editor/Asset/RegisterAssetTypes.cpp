#include "AssetEditor.hpp"
#include "AssetTypes.hpp"
#include "Skore/Core/Registry.hpp"
#include "Handlers/JsonAssetHandler.hpp"
#include "Handlers/MeshAssetHandler.hpp"

namespace Skore
{
    void RegisterSceneAssetHandler();
    void RegisterTextureAssetHandler();
    void RegisterShaderAssetHandlers();
    void RegisterGLTFImporter();
    void RegisterMaterialAssetHandler();

    void RegisterAssetTypes()
    {
        Registry::Type<AssetImporter>();
        Registry::Type<AssetHandler>();
        Registry::Type<JsonAssetHandler>();
        Registry::Type<MeshAssetHandler>();

        RegisterSceneAssetHandler();
        RegisterTextureAssetHandler();
        RegisterShaderAssetHandlers();
        RegisterGLTFImporter();
        RegisterMaterialAssetHandler();
    }
}
