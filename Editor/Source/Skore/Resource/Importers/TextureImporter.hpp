#pragma once
#include "Skore/Graphics/Device.hpp"
#include "Skore/Resource/ResourceAssets.hpp"

namespace Skore
{
	struct TextureImportSettings
	{
		AddressMode wrapMode = AddressMode::Repeat;
		FilterMode  filterMode = FilterMode::Linear;
		bool        overrideIfExists = false;
		bool        async = false;
		bool        isSubAsset = false;
		bool        preserveAlphaCoverage = false;
		f32         alphaCoverageCutoff = 0.5f;
	};

	SK_API RID ImportTexture(RID directory, const TextureImportSettings& settings, const String& path, UndoRedoScope* scope);
	SK_API RID ImportTextureFromMemory(RID directory, const TextureImportSettings& settings, StringView name, Span<u8> data, UndoRedoScope* scope);
}
