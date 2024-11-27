#include <stb_image.h>

#include "JsonAssetHandler.hpp"
#include "Skore/Core/Registry.hpp"
#include "Skore/Editor/Asset/AssetEditor.hpp"
#include "Skore/Editor/Asset/AssetTypes.hpp"
#include "Skore/Editor/Window/TextureViewWindow.hpp"
#include "Skore/Graphics/Assets/TextureAsset.hpp"
#include "Skore/IO/Path.hpp"
#include "TextureAssetHandler.hpp"

#include <stb_image_resize.h>

namespace Skore
{
    struct TextureAssetHandler : JsonAssetHandler
    {
        SK_BASE_TYPES(JsonAssetHandler);

        StringView Extension() override
        {
            return ".texture";
        }

        TypeID GetAssetTypeID() override
        {
            return GetTypeID<TextureAsset>();
        }

        void OpenAsset(AssetFile* assetFile) override
        {
            TextureAsset* textureAsset = Assets::Load<TextureAsset>(assetFile->uuid);
            TextureViewWindow::Open(textureAsset->GetTexture());
        }

        Image GenerateThumbnail(AssetFile* assetFile) override
        {
            //TODO HDR
            TextureAsset* textureAsset = Assets::Load<TextureAsset>(assetFile->uuid);
            Image image = textureAsset->GetImage();
            image.Resize(128, 128);
            return image;
        }
    };


    namespace
    {

        template<typename T>
        struct TextureImportType
        {
        };


        template<>
        struct TextureImportType<u8>
        {
            static Format GetFormat()
            {
                return Format::RGBA;
            }

            static void Resize(const u8* input_pixels, int  input_w, int  input_h, int  input_stride_in_bytes,
                               u8*       output_pixels, int output_w, int output_h, int output_stride_in_bytes,
                               int       num_channels)
            {
                stbir_resize_uint8(input_pixels, input_w, input_h, input_stride_in_bytes,
                                   output_pixels, output_w, output_h, output_stride_in_bytes,
                                   num_channels);
            }
        };

        template<>
        struct TextureImportType<f32>
        {
            static Format GetFormat()
            {
                return Format::RGBA32F;
            }

            static void Resize(const f32* input_pixels, int  input_w, int  input_h, int  input_stride_in_bytes,
                               f32*       output_pixels, int output_w, int output_h, int output_stride_in_bytes,
                               int        num_channels)
            {
                stbir_resize_float(input_pixels, input_w, input_h, input_stride_in_bytes,
                                   output_pixels, output_w, output_h, output_stride_in_bytes,
                                   num_channels);
            }
        };


        template<typename T>
        void ProcessTexture(TextureAsset* texture, OutputFileStream& stream, T* bytes, i32 width, i32 height, u16 channels, bool generateMips)
        {
            texture->compressionMode = CompressionMode::LZ4;

            u32 pixelSize = channels * sizeof(T);
            texture->format = TextureImportType<T>::GetFormat();
            texture->mipLevels = generateMips ? static_cast<u32>(std::floor(std::log2(std::max(width, height)))) + 1 : 1;

            usize totalUncompressedSize{};
            //calc bytes
            {
                u32 mipWidth = width;
                u32 mipHeight = height;
                for (u32 i = 0; i < texture->mipLevels; i++)
                {
                    totalUncompressedSize += mipWidth * mipHeight * pixelSize;
                    if (mipWidth > 1) mipWidth /= 2;
                    if (mipHeight > 1) mipHeight /= 2;
                }
            }

            Array<T> data{};
            data.Resize(width * height * pixelSize);
            MemCopy(data.begin(), bytes, data.Size());

            Array<T> dataToCompress{};
            dataToCompress.Resize(totalUncompressedSize);

            i32 mipWidth = width;
            i32 mipHeight = height;
            u32 offset{};

            for (u32 i = 0; i < texture->mipLevels; i++)
            {
                usize totalMipSize = mipWidth * mipHeight * pixelSize;

                texture->images.EmplaceBack(TextureAssetImage{
                    .byteOffset = offset,
                    .mip = i,
                    .arrayLayer = 0,
                    .extent = Extent{static_cast<u32>(mipWidth), static_cast<u32>(mipHeight)},
                    .size = totalMipSize
                });

                MemCopy(dataToCompress.begin() + offset, data.begin(), totalMipSize);
                texture->totalSize += totalMipSize;

                Array<T> temp = data;

                if (mipWidth > 1 && mipHeight > 1)
                {
                    TextureImportType<T>::Resize(temp.Data(), mipWidth, mipHeight, 0,
                                                 data.Data(), mipWidth / 2, mipHeight / 2, 0,
                                                 channels);
                }

                offset += mipWidth * mipHeight * pixelSize;
                if (mipWidth > 1) mipWidth /= 2;
                if (mipHeight > 1) mipHeight /= 2;
            }

            usize sizeInDisk = Compression::GetMaxCompressedBufferSize(totalUncompressedSize, texture->compressionMode);

            Array<u8> compressedData;
            compressedData.Resize(sizeInDisk);

            texture->totalSizeInDisk = Compression::Compress(compressedData.begin(), sizeInDisk, reinterpret_cast<u8*>(dataToCompress.begin()), totalUncompressedSize, texture->compressionMode);
            stream.Write(compressedData.begin(), texture->totalSizeInDisk);
        }
    }

    struct TextureAssetImporter : AssetImporter
    {
        SK_BASE_TYPES(AssetImporter);

        Array<String> ImportExtensions() override
        {
            return {".png", ".jpg", ".jpeg", ".tga", "bmp", ".hdr"};
        }


        bool ImportAsset(AssetFile* parent, StringView path) override
        {
            AssetFile* assetFile = AssetEditor::CreateAsset(parent, GetTypeID<TextureAsset>(), Path::Name(path));

            TextureAsset* textureAsset = Assets::Load<TextureAsset>(assetFile->uuid);
            TextureImporter::ImportTextureFromFile(assetFile, textureAsset, path);

            return true;
        }
    };

    void RegisterTextureAssetHandler()
    {
        Registry::Type<TextureAssetImporter>();
        Registry::Type<TextureAssetHandler>();
    }

    void TextureImporter::ImportTextureFromMemory(AssetFile* assetFile, TextureAsset* textureAsset, Span<const u8> imageBuffer)
    {
        i32 width{};
        i32 height{};
        i32 channels{};
        u8* bytes = stbi_load_from_memory(imageBuffer.Data(), imageBuffer.Size(), &width, &height, &channels, 4);

        OutputFileStream stream = assetFile->CreateStream();
        ProcessTexture(textureAsset, stream, bytes, width, height, 4, true);
        stream.Close();
        stbi_image_free(bytes);
    }

    void TextureImporter::ImportTextureFromFile(AssetFile* assetFile, TextureAsset* textureAsset, StringView path)
    {
        bool hdr = Path::Extension(path) == ".hdr";

        i32 width{};
        i32 height{};
        i32 channels{};

        OutputFileStream stream = assetFile->CreateStream();

        if (!hdr)
        {
            u8* bytes = stbi_load(path.CStr(), &width, &height, &channels, 4);
            ProcessTexture(textureAsset, stream, bytes, width, height, 4, true);
            stbi_image_free(bytes);
        }
        else
        {
            f32* bytes = stbi_loadf(path.CStr(), &width, &height, &channels, 4);
            ProcessTexture(textureAsset, stream, bytes, width, height, 4, false);
            stbi_image_free(bytes);
        }
        stream.Close();
    }
}
