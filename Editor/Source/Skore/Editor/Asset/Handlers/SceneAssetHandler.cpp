#include "JsonAssetHandler.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Editor/Editor.hpp"
#include "Skore/Editor/Scene/SceneEditor.hpp"
#include "Skore/IO/Path.hpp"
#include "Skore/Scene/Scene.hpp"

namespace Skore
{
    struct SceneAssetHandler : JsonAssetHandler
    {
        SK_BASE_TYPES(JsonAssetHandler);

        StringView Extension() override
        {
            return ".scene";
        }

        TypeID GetAssetTypeID() override
        {
            return GetTypeID<Scene>();
        }

        void OpenAsset(AssetFile* assetFile) override
        {
            Editor::GetSceneEditor().SetScene(assetFile);
        }

        Image GenerateThumbnail(AssetFile* assetFile) override
        {
            return {};
        }
    };

    void RegisterSceneAssetHandler()
    {
        Registry::Type<SceneAssetHandler>();
    }
}
