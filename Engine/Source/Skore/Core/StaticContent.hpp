#pragma once

#include "StringView.hpp"
#include "String.hpp"
#include "Image.hpp"
#include "Skore/Graphics/GraphicsTypes.hpp"

namespace Skore::StaticContent
{
    SK_API Array<u8> GetBinaryFile(StringView path);
    SK_API String    GetTextFile(StringView path);
    SK_API Image     GetImageFile(StringView path);
    SK_API Texture   GetTextureFile(StringView path);
}
