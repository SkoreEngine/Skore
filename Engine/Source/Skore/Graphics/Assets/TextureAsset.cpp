#include "TextureAsset.hpp"

#include "Skore/Core/Logger.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Graphics/Graphics.hpp"

namespace Skore
{
    namespace
    {
        Logger& logger = Logger::GetLogger("Skore::TextureAsset");
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
            logger.Debug("starting loading texture {}", GetName());
            Array<u8> textureBytes = GetTextureBytes();

            logger.Debug("bytes loaded {}", GetName());
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

            logger.Debug("texture created {}", GetName());

            Graphics::UpdateTextureData(TextureDataInfo{
                .texture = texture,
                .data = textureBytes.Data(),
                .size = textureBytes.Size(),
                .regions = regions
            });

            logger.Debug("texture data uploaded {}", GetName());
            logger.Debug("------------------------------");
        }
        return texture;
    }

    Array<u8> TextureAsset::GetTextureBytes() const
    {
        if (compressionMode != CompressionMode::None)
        {
            Array<u8> diskBuffer = LoadStream(0, totalSizeInDisk);
            logger.Debug("stream loaded {}", GetName());

            Array<u8> textureBytes;
            textureBytes.Resize(totalSize);

            Compression::Decompress(textureBytes.begin(), totalSize, diskBuffer.begin(), totalSizeInDisk, compressionMode);
            logger.Debug("decompressed {}", GetName());

            return textureBytes;
        }

        return LoadStream(0, totalSizeInDisk);
    }

    Image TextureAsset::GetImage() const
    {
        const TextureAssetImage& textureImage = images[0];
        Image image(textureImage.extent.width, textureImage.extent.height, 4);
        image.data = GetTextureBytes();
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
