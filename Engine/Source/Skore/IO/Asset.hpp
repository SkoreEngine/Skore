#pragma once

#include "Skore/Common.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/UUID.hpp"


namespace Skore
{
    struct AssetLoader;

    class SK_API Asset : public Object
    {
    public:
        ~Asset() override = default;

        UUID              GetUUID() const;
        void              SetUUID(UUID uuid);
        StringView        GetName() const;
        TypeHandler*      GetTypeHandler() const;
        virtual usize     LoadStream(usize offset, usize size, Array<u8>& array) const;
        void              SetTypeHandler(TypeHandler* typeHandler);
        void              SetLoader(AssetLoader* loader);
        virtual void      OnChange() {}

        friend class Assets;

    private:
        UUID         uuid;
        TypeHandler* typeHandler = nullptr;
        AssetLoader* loader = nullptr;
    };

    struct SK_API AssetLoader
    {
        virtual ~AssetLoader() = default;

        virtual Asset*     LoadAsset() = 0;
        virtual void       Reload(Asset* asset) {}
        virtual usize      LoadStream(usize offset, usize size, Array<u8>& array) = 0;
        virtual StringView GetName() = 0;
    };

    class SK_API Assets
    {
    public:
        static void   Create(UUID uuid, AssetLoader* loader);
        static Asset* Get(UUID uuid);
        static Asset* Load(UUID uuid);
        static Asset* LoadNoCache(UUID uuid);
        static void   Unload(UUID uuid);
        static Asset* Reload(UUID uuid);
        static Asset* LoadByPath(StringView path);
        static void   SetPath(UUID uuid, StringView path);

        template <typename T>
        static T* LoadByPath(StringView path)
        {
            return static_cast<T*>(LoadByPath(path));
        }

        template <typename T>
        static T* Load(UUID uuid)
        {
            return static_cast<T*>(Load(uuid));
        }

        template <typename T>
        static T* Get(UUID uuid)
        {
            return static_cast<T*>(Get(uuid));
        }
    };

    template <typename T>
    struct ArchiveType<T*, Traits::EnableIf<Traits::IsBaseOf<Asset, T>>>
    {
        constexpr static bool hasArchiveImpl = true;

        static ArchiveValue ToValue(ArchiveWriter& writer, const T* value)
        {
            if (value)
            {
                return ArchiveType<UUID>::ToValue(writer, value->GetUUID());
            }
            return {};
        }

        static void FromValue(ArchiveReader& reader, ArchiveValue archiveValue, T*& typeValue)
        {
            UUID uuid = {};
            ArchiveType<UUID>::FromValue(reader, archiveValue, uuid);
            typeValue = Assets::Load<T>(uuid);
        }
    };


    struct AssetApi
    {
        Asset* (*CastAsset)(VoidPtr ptr);
        void (*  SetAsset)(VoidPtr ptr, Asset* asset);
    };

    template <typename T>
    struct TypeApiInfo<T, Traits::EnableIf<Traits::IsBaseOf<Asset, T>>>
    {
        static void ExtractApi(VoidPtr pointer)
        {
            AssetApi* api = static_cast<AssetApi*>(pointer);
            api->CastAsset = [](VoidPtr ptr)
            {
                return static_cast<Asset*>(*static_cast<T**>(ptr));
            };

            api->SetAsset = [](VoidPtr ptr, Asset* asset)
            {
                *static_cast<T**>(ptr) = static_cast<T*>(asset);
            };
        }

        static constexpr TypeID GetApiId()
        {
            return GetTypeID<AssetApi>();
        }
    };
}
