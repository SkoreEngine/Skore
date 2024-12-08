#include "TextureAsset.hpp"

#include "Skore/Core/Chronometer.hpp"
#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Graphics/Graphics.hpp"

namespace Skore
{
    namespace
    {
        Logger& logger = Logger::GetLogger("Skore::TextureAsset");
        Array<u8> diskBuffer;
        Array<u8> textureBytes;
    }


    TextureAsset::~TextureAsset()
    {
        if (texture)
        {
            Graphics::DestroyTexture(texture);
        }
    }

    Texture TextureAsset::GetTexture()
    {
        if (!texture)
        {
            Chronometer c;

            LoadTextureBytes();

            if (textureBytes.Size() == 0)
            {
                return {};
            }

            texture = Graphics::CreateTexture(TextureCreation{
                .extent = {images[0].extent.width, images[0].extent.height, 1},
                .format = format,
                .mipLevels = std::max(mipLevels, 1u),
                .arrayLayers = 1u,
            });

            Array<TextureDataRegion> regions{};
            regions.Reserve(images.Size());

            for (const TextureAssetImage& textureAssetImage : images)
            {
                regions.EmplaceBack(TextureDataRegion{
                    .dataOffset = textureAssetImage.byteOffset,
                    .mipLevel = textureAssetImage.mip,
                    .arrayLayer = textureAssetImage.arrayLayer,
                    .extent = Extent3D{textureAssetImage.extent.width, textureAssetImage.extent.height, 1},
                });
            }

            {
                Chronometer d;
                Graphics::UpdateTextureData(TextureDataInfo{
                    .texture = texture,
                    .data = textureBytes.Data(),
                    .size = totalSize,
                    .regions = regions
                });
            }
            logger.Debug("time spend to load {} {}ms ", GetName(), c.Diff());
        }
        return texture;
    }

    void TextureAsset::LoadTextureBytes() const
    {
        if (compressionMode != CompressionMode::None)
        {
            LoadStream(0, totalSizeInDisk, diskBuffer);

            if (totalSize > textureBytes.Size())
            {
                textureBytes.Resize(totalSize);
            }
            Compression::Decompress(textureBytes.begin(), totalSize, diskBuffer.begin(), totalSizeInDisk, compressionMode);
            return;
        }
        LoadStream(0, totalSizeInDisk, textureBytes);
    }

    Image TextureAsset::GetImage() const
    {
        const TextureAssetImage& textureImage = images[0];
        Image image(textureImage.extent.width, textureImage.extent.height, 4);
        LoadTextureBytes();
        image.data = Span{textureBytes.Data(), totalSize};
        return image;
    }

    void TextureAsset::RegisterType(NativeTypeHandler<TextureAsset>& type)
    {
        type.Field<&TextureAsset::images>("images");
        type.Field<&TextureAsset::format>("format");
        type.Field<&TextureAsset::mipLevels>("mipLevels");
        type.Field<&TextureAsset::totalSize>("totalSize");
        type.Field<&TextureAsset::totalSizeInDisk>("totalSizeInDisk");
        type.Field<&TextureAsset::compressionMode>("compressionMode");
    }

    void TextureAssetImage::RegisterType(NativeTypeHandler<TextureAssetImage>& type)
    {
        type.Field<&TextureAssetImage::byteOffset>("byteOffset");
        type.Field<&TextureAssetImage::mip>("mip");
        type.Field<&TextureAssetImage::arrayLayer>("arrayLayer");
        type.Field<&TextureAssetImage::extent>("extent");
        type.Field<&TextureAssetImage::size>("size");
    }
}
