#pragma once

#include "Skore/Core/Array.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Core/String.hpp"

namespace Skore
{
	class GPUTexture;
}

namespace Skore::StaticContent
{
	struct Image
	{
		Array<u8> pixels;
		i32       width = 0;
		i32       height = 0;
	};

	SK_API Array<u8>   GetBinaryFile(StringView path);
	SK_API String      GetTextFile(StringView path);
	SK_API GPUTexture* GetTexture(StringView path);
	SK_API Image       GetImage(StringView path);
	SK_API void        SaveFilesToDirectory(StringView path, StringView directory);
}
