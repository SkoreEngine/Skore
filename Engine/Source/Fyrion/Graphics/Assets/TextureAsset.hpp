#pragma once

#include "Fyrion/Common.hpp"

#include "Fyrion/Graphics/GraphicsTypes.hpp"
#include "Fyrion/IO/Asset.hpp"
#include "Fyrion/IO/Compression.hpp"

namespace Fyrion
{
    struct TextureAssetImage
    {
        u32    byteOffset{};
        u32    mip{};
        u32    arrayLayer{};
        Extent extent{};
        usize  size{};

        static void RegisterType(NativeTypeHandler<TextureAssetImage>& type);
    };

    class FY_API TextureAsset : public Asset
    {
    public:
        FY_BASE_TYPES(Asset);

        ~TextureAsset() override;

        Texture GetTexture();
        Image   GetImage() const;

        static void RegisterType(NativeTypeHandler<TextureAsset>& type);

        Array<TextureAssetImage> images{};
        Format                   format = Format::Undefined;
        u32                      mipLevels = 0;
        u64                      totalSize = 0;
        u64                      totalSizeInDisk = 0;
        CompressionMode          compressionMode = CompressionMode::None;

    private:
        Texture texture = {};


        Array<u8> GetTextureBytes() const;
    };
}
