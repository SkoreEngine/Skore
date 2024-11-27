#pragma once

namespace Skore
{
    class TextureAsset;
}


namespace Skore::TextureImporter
{
    void ImportTextureFromMemory(AssetFile* assetFile, TextureAsset* textureAsset, Span<const u8> imageBuffer);
    void ImportTextureFromFile(AssetFile* assetFile, TextureAsset* textureAsset, StringView path);
}