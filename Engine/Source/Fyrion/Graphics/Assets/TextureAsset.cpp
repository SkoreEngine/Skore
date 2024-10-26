#include "TextureAsset.hpp"

#include "Fyrion/Core/Registry.hpp"
#include "Fyrion/Graphics/Graphics.hpp"

namespace Fyrion
{
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
            Array<u8> textureBytes = LoadStream(0, totalSizeInDisk);
            if (textureBytes.Size() == 0)
            {
                return {};
            }

            Texture texture = Graphics::CreateTexture(TextureCreation{
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

            Graphics::UpdateTextureData(TextureDataInfo{
                .texture = texture,
                .data = textureBytes.Data(),
                .size = textureBytes.Size(),
                .regions = regions
            });

            return texture;
        }
        return texture;
    }

    Image TextureAsset::GetImage() const
    {
        const TextureAssetImage& textureImage = images[0];
        Image image(textureImage.extent.width, textureImage.extent.height, 4);
        image.data = LoadStream(0, textureImage.size);
        return image;
    }

    void TextureAsset::RegisterType(NativeTypeHandler<TextureAsset>& type)
    {
        type.Field<&TextureAsset::images>("images");
        type.Field<&TextureAsset::format>("format");
        type.Field<&TextureAsset::mipLevels>("mipLevels");
        type.Field<&TextureAsset::totalSizeInDisk>("totalSizeInDisk");
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
