#pragma once

namespace Fyrion
{
    class TextureAsset;
}


namespace Fyrion::TextureImporter
{
    void ImportTextureFromMemory(AssetFile* assetFile, TextureAsset* textureAsset, Span<const u8> imageBuffer);
    void ImportTextureFromFile(AssetFile* assetFile, TextureAsset* textureAsset, StringView path);
}