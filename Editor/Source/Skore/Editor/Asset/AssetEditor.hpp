#pragma once
#include "AssetTypes.hpp"
#include "Skore/Core/Array.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/UUID.hpp"
#include "Skore/Core/Span.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"
#include "Skore/IO/Asset.hpp"
#include "Skore/IO/FileTypes.hpp"


namespace Skore
{

    enum class AssetType
    {
        Asset,
        Source
    };

    struct AssetFile : AssetLoader
    {
        u32    hash;
        String fileName;
        String extension;
        String path;
        String absolutePath;
        String tempBuffer;
        bool   isDirectory;
        UUID   uuid;

        u64 currentVersion;
        u64 persistedVersion;

        Array<AssetFile*> children;

        AssetFile* parent;

        AssetHandler* handler;

        bool active = true;
        bool canAcceptNewAssets = true;

        AssetType assetType = AssetType::Asset;

        bool IsDirty() const;
        void RemoveFromParent();

        Texture thumbnail = {};
        bool    thumbnailVerified = false;

        Asset*     LoadAsset() override;
        void       Reload(Asset* asset) override;
        usize      LoadStream(usize offset, usize size, Array<u8>& array) override;
        StringView GetName() override;
        String     MakePathName() const;

        OutputFileStream CreateStream();

        Texture GetThumbnail();

        void MoveTo(AssetFile* newParent);
        bool IsChildOf(AssetFile* item) const;
        void Destroy();

        void UpdatePath();
        bool IsNewAsset() const;

        ~AssetFile() override;
    };


    namespace AssetEditor
    {
        SK_API void             AddPackage(StringView name, StringView directory);
        SK_API void             SetProject(StringView name, StringView directory);
        SK_API Span<AssetFile*> GetPackages();
        SK_API AssetFile*       GetProject();
        SK_API AssetFile*       GetAssetFolder();
        SK_API AssetFile*       CreateDirectory(AssetFile* parent);
        SK_API AssetFile*       CreateAsset(AssetFile* parent, TypeID typeId, StringView suggestedName = "");
        SK_API void             UpdateAssetValue(AssetFile* assetFile, Asset* asset);
        SK_API void             Rename(AssetFile* assetFile, StringView newName);
        SK_API void             GetUpdatedAssets(Array<AssetFile*>& updatedAssets);
        SK_API void             SaveAssets(Span<AssetFile*> assetsToSave);
        SK_API void             DeleteAssets(Span<AssetFile*> assetFile);
        SK_API String           CreateUniqueName(AssetFile* parent, StringView desiredName);
        SK_API void             ImportAssets(AssetFile* parent, Span<String> paths);
        SK_API void             FilterExtensions(Array<FileFilter>& extensions);
        SK_API Span<AssetFile*> GetAssetsOfType(TypeID typeId);
        SK_API AssetFile*       FindAssetFileByUUID(UUID uuid);
        SK_API String           GetTempFolder();

        SK_API void             Export(StringView directory);

        SK_API void             CreateCMakeProject();
        SK_API bool             CanCreateCMakeProject();
    }
}
