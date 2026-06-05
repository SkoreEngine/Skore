#pragma once
#include "Skore/Common.hpp"
#include "Skore/Core/StringView.hpp"
#include "Skore/Resource/Importers/MeshImporter.hpp"
#include "Skore/Resource/Importers/TextureImporter.hpp"


namespace Skore
{
	struct UndoRedoScope;
	struct CookContext;

	struct FBXImportSettings
	{
		MeshImportSettings    mesh{.generateUV1s = true};
		TextureImportSettings texture;
		bool                  importAnimations = true;
	};

	SK_API bool ImportFBX(CookContext& cookCtx, const FBXImportSettings& settings, StringView path);
}
