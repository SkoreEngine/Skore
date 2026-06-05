#pragma once
#include "Skore/Graphics/Device.hpp"
#include "Skore/Resource/ResourceAssets.hpp"

namespace Skore
{
	struct TextureImportSettings
	{
		AddressMode wrapMode = AddressMode::Repeat;
		FilterMode  filterMode = FilterMode::Linear;
		bool        preserveAlphaCoverage = false;
		f32         alphaCoverageCutoff = 0.5f;
	};

	struct TextureImportOptions
	{
		bool overrideIfExists = false;
		bool async = false;
		bool isSubAsset = false;
	};

	SK_API RID ImportTexture(RID directory, const TextureImportSettings& settings, const TextureImportOptions& options, const String& path, UndoRedoScope* scope);
	SK_API RID ImportTextureFromMemory(RID directory, const TextureImportSettings& settings, const TextureImportOptions& options, StringView name, Span<u8> data, UndoRedoScope* scope);
}
